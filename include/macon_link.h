#pragma once

// ---------------------------------------------------------------------------
// MaconLink — the transport/transaction layer (Layer 2).
//
// The pure core (tuya_codec + macon_registers + macon_state) knows the wire
// format and the register map but nothing about I/O. MaconLink adds the
// request/response *transaction* logic — "set the hot-water setpoint and wait
// for the mainboard's ACK", "read the cooling setpoint back" — on top of an
// injected byte Transport that the consumer implements over its concrete UART.
//
// Layering:
//   1. tuya_codec / macon_* : pure frame + register knowledge (no I/O).
//   2. MaconLink            : this file — synchronous write/read transactions
//                             over an injected MaconTransport. Still pure and
//                             host-testable with a fake transport.
//   3. Consumer             : owns the real ESP-IDF UART and implements
//                             MaconTransport. arctic-controller is the bus
//                             master; the sniffer/simulator do not need this.
//
// The API is INTENT-named: a caller says `set_hot_water_setpoint(50)`, not
// "encode an fc=0x06 frame". No Tuya/Macon protocol detail leaks into the
// method names.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

#include "tuya_codec.h"

namespace arctic {

// ---------------------------------------------------------------------------
// Injected byte transport (consumer owns the concrete UART)
// ---------------------------------------------------------------------------

class MaconTransport {
public:
    virtual ~MaconTransport() = default;

    /// Write exactly `n` bytes to the bus. Return the number of bytes written
    /// (`n` on success), or a negative value on error.
    virtual int write(const uint8_t *data, size_t n) = 0;

    /// Read up to `n` bytes into `buf`, blocking at most `timeout_ms`. Return
    /// the number of bytes read (0 on timeout with no data), or a negative
    /// value on error.
    virtual int read(uint8_t *buf, size_t n, int timeout_ms) = 0;
};

// ---------------------------------------------------------------------------
// Transaction results
// ---------------------------------------------------------------------------

enum class MaconResult {
    Ok,            // write ACKed / read value returned
    WriteFailed,   // transport.write() failed or the frame could not be built
    NoResponse,    // no valid matching frame arrived before the timeout
    BadResponse,   // a frame arrived but did not match the expected addr/count,
                   // or a read response was too short to hold the field
    Nack,          // the unit replied with an fc-exception frame (fc | 0x80).
                   // SPECULATIVE: a NACK has never been observed on this unit;
                   // its exact shape is unconfirmed (see docs/REGISTERS.md and
                   // the `macon-nack-probe` follow-up). Detection here is
                   // best-effort against the Modbus-style exception convention.
};

/// Human-readable name for a MaconResult ("Ok", "NoResponse", ...).
const char *macon_result_name(MaconResult r);

// ---------------------------------------------------------------------------
// MaconLink
// ---------------------------------------------------------------------------

class MaconLink {
public:
    /// `transport` must outlive the MaconLink. `response_timeout_ms` bounds how
    /// long each read/ack wait blocks on the transport.
    explicit MaconLink(MaconTransport &transport, int response_timeout_ms = 600);

    // --- setpoint writes (controller -> unit) ------------------------------
    // Each issues an fc=0x06 single-byte write to the setpoint's wire address
    // and waits for the mainboard's echoing ACK. Returns Ok only when a
    // matching (non-exception) ACK is received. `celsius` is a whole-°C value
    // (clamped to the signed-byte wire range).

    /// Cooling setpoint — wire addr 0x0000 (reg2093). Confirmed live.
    MaconResult set_cooling_setpoint(int celsius);

    /// Hot-water setpoint — wire addr 0x0002 (reg2095). Confirmed live.
    MaconResult set_hot_water_setpoint(int celsius);

    // --- setpoint reads (controller -> unit) -------------------------------
    // Each issues an fc=0x03 read of the telemetry window (wire addr 0,
    // count 50) and extracts the setpoint byte. Returns Ok and writes
    // `*out_celsius` on success.

    /// Cooling setpoint read-back — reg2093 (telemetry byte 0). Confirmed.
    MaconResult read_cooling_setpoint(int *out_celsius);

    /// Hot-water setpoint read-back — reg2095 (telemetry byte 2). Confirmed.
    MaconResult read_hot_water_setpoint(int *out_celsius);

    /// Space-heating setpoint read-back — reg2094 (telemetry byte 1).
    ///
    /// UNVERIFIED: reg2094 has NOT been confirmed to be the space-heating
    /// setpoint. It may instead be a backup/aux-heater setpoint, and the test
    /// unit lacks that stage, so the returned value is UNTESTED. Provided so a
    /// future consumer with heating hardware can exercise/confirm it. There is
    /// deliberately NO `set_heating_setpoint()` until the register is proven.
    MaconResult read_heating_setpoint(int *out_celsius);

private:
    // Write one signed-byte setpoint to `wire_addr`, wait for the fc=0x06 ACK.
    MaconResult write_setpoint(uint16_t wire_addr, int celsius);
    // Read the telemetry window and return telemetry byte `byte_offset`
    // (== wire addr) as a signed whole-°C int.
    MaconResult read_setpoint(uint16_t byte_offset, int *out_celsius);
    // Read bytes from the transport until a checksum-valid frame matching
    // (want_dir, want_fc, want_a, want_b) is found, or timeout / NACK.
    // `store` (capacity `cap`) backs `out.payload`, so it must outlive use.
    MaconResult read_matching_frame(tuya_codec::ParsedFrame &out,
                                    uint8_t *store, size_t cap,
                                    uint8_t want_dir, uint8_t want_fc,
                                    uint16_t want_a, uint16_t want_b);

    MaconTransport &tx_;
    int timeout_ms_;
};

}  // namespace arctic
