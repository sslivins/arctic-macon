#pragma once

// ---------------------------------------------------------------------------
// Tuya MCU framing codec for the Arctic heat pump wire protocol.
//
// Pure, dependency-free implementation: no ESP-IDF, no FreeRTOS, no UART.
// Designed to be linked into the sniffer (decode), the simulator (decode +
// encode responses), and the controller (encode requests + decode responses),
// AND to compile natively for unit tests on a host machine.
//
// Wire format (validated against captures, 2026-05-03):
//
//   55 AA <dir:1> <fc:1> <addr:2BE> <count:2BE> [data:count bytes] <chk:1>
//
//   dir   : 0xF0 = controller -> heat pump (request)
//           0x0F = heat pump -> controller (response)
//   fc    : 0x03 = read register block
//           0x06 = write register block (controller -> unit). Confirmed live
//                  2026-07-08: a setpoint change emits
//                  `55 AA F0 06 0000 0001 <°C> <chk>` (addr 0, count 1, one
//                  data byte = the new setpoint in whole °C). The unit ACKs
//                  with a 9-byte `55 AA 0F 06 <addr:2> <count:2> <chk>` that
//                  echoes addr+count but carries NO data byte. So an fc=0x06
//                  REQUEST length = HDR_LEN + count + CHK_LEN, while an fc=0x06
//                  RESPONSE length = HDR_LEN + CHK_LEN (like Modbus FC16).
//   addr  : starting BYTE offset into the unified Arctic register page.
//           NOTE: this is NOT a Modbus register number; it is a byte offset.
//           Currently only two values seen: 0 (telemetry window) and 50
//           (holding window).
//   count : number of payload bytes in the response. Request frames carry
//           no payload (the request length is fixed at 9 bytes).
//   chk   : 1 byte. chk = ~sum(bytes_after_55AA_through_byte_before_chk) & 0xFF.
//
// Frame total length:
//   Request  = HDR_LEN + CHK_LEN              = 9 bytes
//   Response = HDR_LEN + count + CHK_LEN      = 9 + count bytes
//
// Two register windows are observed in rotation:
//
//   addr=0,  count=50 -> "telemetry" (regs 2093..2142):
//                        byte 0 = reg 2093 = COOLING SETPOINT (whole °C).
//                                 Confirmed live 2026-07-08: flipped 0x0C (12)
//                                 -> 0x18 (24) the instant the controller's
//                                 setpoint dial moved 12->24 °C.
//                        byte 1 = reg 2094 (0x28=40; likely heating setpoint,
//                                 UNCONFIRMED).
//                        byte 2 = reg 2095 (0x32=50; mirrors the hot-water
//                                 setpoint, UNCONFIRMED).
//                        bytes 3..6 = regs 2096..2099 (not yet decoded).
//                        byte 7 = reg 2100, byte 8 = reg 2101, ... byte 49 =
//                                 reg 2142.
//                        (Formerly bytes 0..6 were skipped as a "static
//                        prefix"; byte 0 is NOT static — it is the setpoint.)
//
//   addr=50, count=58 -> "holding" (regs 2000..2057):
//                        no prefix; byte 0 = reg 2000, byte 1 = reg 2001, ...
//
// Each Arctic "register" on the wire is 1 byte (NOT 2 bytes like classic
// Modbus). Per-register scaling and signedness are defined elsewhere
// (see arctic_registers.cpp).
//
// ---------------------------------------------------------------------------
// Trailing "block-tag" byte (safe to ignore)  [investigated 2026-07-07]
// ---------------------------------------------------------------------------
// After each *response* (dir=0x0F), the heat pump appends ONE extra byte
// AFTER the checksum, outside the frame:
//
//   59-byte telemetry response (addr=0,  count=50) -> trailer 0x14
//   67-byte holding   response (addr=50, count=58) -> trailer 0x00
//
// Findings (arctic-sniffer v0.3.6, live capture on the real Macon controller,
// /api/skipped inter-byte timing):
//   * The heat pump SENDS it, not the controller. Every trailer arrives in the
//     same continuous burst as the response tail (gap_before = 0 us), followed
//     by a full ~500 ms poll gap before the controller's next request. A
//     controller ACK would show the opposite timing (turnaround gap BEFORE the
//     byte). Measurement has microsecond resolution, so this is unambiguous.
//   * It is deterministic PER WINDOW TYPE (0x14 telemetry / 0x00 holding), not
//     per payload -> it is a block/window-type tag, NOT a checksum, byte count
//     (0x14=20 != 59), CRC, echoed register, or line noise.
//   * It sits AFTER chk, so it never affects frame validation. The decoder
//     simply resyncs past it; the following frame always decodes cleanly.
//
// Conclusion: safe to ignore. The codec treats it as a skipped inter-frame
// byte. One open (non-blocking) question: whether 0x14 is a fixed tag or
// coincidentally echoes holding reg 2051 (P43 "3-Way Valve Time" = 20s). That
// needs a physical P43 change at the controller to confirm; not worth the
// effort unless the trailer value is ever seen to vary.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

namespace tuya_codec {

// ---------------------------------------------------------------------------
// Frame constants
// ---------------------------------------------------------------------------

constexpr uint8_t HDR0          = 0x55;
constexpr uint8_t HDR1          = 0xAA;
constexpr uint8_t DIR_REQUEST   = 0xF0;
constexpr uint8_t DIR_RESPONSE  = 0x0F;
constexpr uint8_t FC_READ       = 0x03;
constexpr uint8_t FC_CMD        = 0x06;  // controller command (power/mode/setpoint)

constexpr size_t  HDR_LEN       = 8;    // 55 AA dir fc addr:2 count:2
constexpr size_t  CHK_LEN       = 1;
constexpr size_t  MIN_FRAME_LEN = HDR_LEN + CHK_LEN;   // 9 (request)
constexpr size_t  MAX_FRAME_LEN = 256;

// ---------------------------------------------------------------------------
// Register windows: maps the wire's (addr,count) pair to an Arctic register
// base address and a byte prefix to skip.
// ---------------------------------------------------------------------------

struct RegWindow {
    uint16_t field_a;     // wire `addr` field
    uint16_t field_b;     // wire `count` field
    uint16_t reg_base;    // first Arctic register number this window carries
    uint8_t  prefix_len;  // payload bytes to skip before register data starts
};

constexpr RegWindow KNOWN_WINDOWS[] = {
    { 0,  50, 2093, 0 },  // telemetry (byte0 = reg2093 cooling setpoint)
    { 50, 58, 2000, 0 },  // holding regs
};
constexpr size_t KNOWN_WINDOWS_COUNT = sizeof(KNOWN_WINDOWS) / sizeof(KNOWN_WINDOWS[0]);

/// Look up the window for a given (addr, count) tuple. Returns nullptr if
/// the tuple does not correspond to a recognised window.
const RegWindow *find_window(uint16_t field_a, uint16_t field_b);

// ---------------------------------------------------------------------------
// Checksum
// ---------------------------------------------------------------------------

/// Compute the 1-byte Tuya checksum over a complete frame buffer.
/// Sums bytes [2 .. frame_len - 2] (i.e. everything after the 55 AA magic,
/// up to but not including the checksum slot), then returns ~sum & 0xFF.
uint8_t compute_checksum(const uint8_t *frame, size_t frame_len);

// ---------------------------------------------------------------------------
// Frame length
// ---------------------------------------------------------------------------

/// Compute the expected total frame length given the dir byte and the
/// count field (`field_b`). Returns 0 if dir is unknown or the length
/// would exceed MAX_FRAME_LEN. Valid for fc=0x03 (read) frames only; use
/// `command_frame_len()` for fc=0x06 (command/write) frames.
size_t frame_total_len(uint8_t dir, uint16_t field_b);

/// Compute the expected total frame length of an fc=0x06 command frame.
/// A REQUEST (dir=0xF0) carries `count` inline data bytes, so its length is
/// HDR_LEN + count + CHK_LEN. A RESPONSE/ACK (dir=0x0F) echoes addr+count with
/// no data, so its length is HDR_LEN + CHK_LEN. Returns 0 for an unknown dir
/// or an over-long frame.
size_t command_frame_len(uint8_t dir, uint16_t count);

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

enum class ParseResult {
    OK,
    BAD_MAGIC,         // first two bytes are not 55 AA
    TRUNCATED,         // buf is shorter than the declared frame length
    BAD_DIR,           // dir is not 0xF0 or 0x0F
    BAD_FC,            // fc is not 0x03
    UNKNOWN_WINDOW,    // (addr, count) tuple is not in KNOWN_WINDOWS
    BAD_CHECKSUM,      // computed checksum does not match the trailing byte
};

struct ParsedFrame {
    uint8_t        dir;          // DIR_REQUEST or DIR_RESPONSE
    uint8_t        fc;           // function code (currently always FC_READ)
    uint16_t       field_a;      // wire addr field (byte offset)
    uint16_t       field_b;      // wire count field (payload byte count)
    const RegWindow *window;     // matched window (never null on OK)
    const uint8_t *payload;      // pointer to first payload byte, or nullptr
                                 // for frames that carry no payload (read
                                 // requests, and fc=0x06 ACK responses)
    size_t         payload_len;  // = field_b for read responses and fc=0x06
                                 // write requests, 0 otherwise
    size_t         frame_len;    // total frame length consumed from buf
    uint8_t        checksum;     // received checksum byte
};

/// Parse a single Tuya frame from `buf` (which must start at the 55 AA magic).
/// On OK, fills `out` and returns ParseResult::OK with out.frame_len bytes
/// consumed. On any error, `out` is left in an unspecified state.
///
/// `buf_len` is the number of bytes available in buf. If buf_len is less
/// than the declared frame length, returns TRUNCATED (the caller should
/// wait for more bytes before retrying).
ParseResult parse_frame(const uint8_t *buf, size_t buf_len, ParsedFrame &out);

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

/// Encode a request frame (9 bytes: 55 AA F0 fc addr:2 count:2 chk).
/// Returns the number of bytes written, or 0 on error
/// (buf_capacity < MIN_FRAME_LEN, unknown window).
size_t encode_request(uint8_t *buf, size_t buf_capacity,
                      uint8_t fc, uint16_t field_a, uint16_t field_b);

/// Encode a response frame (HDR_LEN + field_b + CHK_LEN bytes).
/// `payload` must point to exactly `field_b` bytes, which are copied
/// verbatim into the frame. Returns the number of bytes written, or 0
/// on error (buf_capacity too small, unknown window, payload null when
/// field_b > 0).
size_t encode_response(uint8_t *buf, size_t buf_capacity,
                       uint8_t fc, uint16_t field_a, uint16_t field_b,
                       const uint8_t *payload);

/// Encode an fc=0x06 command (write) REQUEST frame from the controller to the
/// unit: `55 AA F0 06 <field_a:2BE> <count:2BE> <data:count> <chk>`.
/// `data` must point to exactly `count` bytes (may be null iff count == 0).
/// For a setpoint write this is field_a=0x0000, count=1, data={celsius}.
/// Returns bytes written (HDR_LEN + count + CHK_LEN), or 0 on error
/// (null buf, null data with count>0, buffer too small, over-long frame).
size_t encode_command(uint8_t *buf, size_t buf_capacity,
                      uint16_t field_a, uint16_t count, const uint8_t *data);

/// Encode an ACK for a controller command (fc=0x06). The mainboard replies
/// with a fixed 9-byte frame `55 AA 0F 06 <field_a:2BE> <field_b:2BE> <chk>`,
/// echoing the command's addr (`field_a`) and register count (`field_b`) with
/// NO data byte. Returns bytes written, or 0 if buf_capacity < MIN_FRAME_LEN.
size_t encode_command_ack(uint8_t *buf, size_t buf_capacity,
                          uint16_t field_a, uint16_t field_b);

// ---------------------------------------------------------------------------
// Frame discovery (for streaming byte sources)
// ---------------------------------------------------------------------------

/// Find the first plausible frame-start offset in `buf`. A plausible start
/// is `55 AA` followed by a header that passes a basic sanity check
/// (known dir, known fc, known window). Returns `buf_len` if no candidate
/// is found (caller should keep accumulating bytes).
///
/// Use this with a streaming UART loop: scan for a plausible start, then
/// call `parse_frame()` to validate the checksum and extract the payload.
size_t find_frame_start(const uint8_t *buf, size_t buf_len);

}  // namespace tuya_codec
