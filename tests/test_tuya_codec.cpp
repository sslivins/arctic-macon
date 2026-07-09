// Native unit test for the Tuya-55AA codec, focused on fc=0x06 command
// (write) frames — the path used to set the cooling setpoint on the mainboard.
// Framework-free: prints failures and returns non-zero on any failure.

#include "tuya_codec.h"

#include <cstdio>
#include <cstring>

using namespace tuya_codec;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

int main() {
    // The definitive live capture (2026-07-08): the controller wrote setpoint
    // 24 °C as `55 AA F0 06 00 00 00 01 18 F0`, and the unit ACKed with
    // `55 AA 0F 06 00 00 00 01 E9`.
    const uint8_t kWrite[10] =
        { 0x55, 0xAA, 0xF0, 0x06, 0x00, 0x00, 0x00, 0x01, 0x18, 0xF0 };
    const uint8_t kAck[9] =
        { 0x55, 0xAA, 0x0F, 0x06, 0x00, 0x00, 0x00, 0x01, 0xE9 };

    // --- command_frame_len -------------------------------------------------
    CHECK(command_frame_len(DIR_REQUEST, 1) == 10);   // 8 hdr + 1 data + 1 chk
    CHECK(command_frame_len(DIR_REQUEST, 0) == 9);
    CHECK(command_frame_len(DIR_RESPONSE, 1) == 9);    // ACK carries no data
    CHECK(command_frame_len(0x99, 1) == 0);            // unknown dir

    // --- checksum matches the captured frames ------------------------------
    CHECK(compute_checksum(kWrite, sizeof(kWrite)) == 0xF0);
    CHECK(compute_checksum(kAck, sizeof(kAck)) == 0xE9);

    // --- encode_command: byte-exact write frame ----------------------------
    {
        uint8_t buf[16];
        const uint8_t data = 0x18;   // 24 °C
        size_t n = encode_command(buf, sizeof(buf), 0x0000, 1, &data);
        CHECK(n == 10);
        CHECK(std::memcmp(buf, kWrite, 10) == 0);

        // count 0 -> data may be null; 9-byte frame.
        n = encode_command(buf, sizeof(buf), 0x0000, 0, nullptr);
        CHECK(n == 9);

        // error paths
        CHECK(encode_command(nullptr, sizeof(buf), 0, 1, &data) == 0);
        CHECK(encode_command(buf, 4, 0, 1, &data) == 0);          // too small
        CHECK(encode_command(buf, sizeof(buf), 0, 1, nullptr) == 0); // null data
    }

    // --- parse_frame: fc=0x06 write request (10 bytes, has payload) --------
    {
        ParsedFrame pf;
        ParseResult r = parse_frame(kWrite, sizeof(kWrite), pf);
        CHECK(r == ParseResult::OK);
        CHECK(pf.dir == DIR_REQUEST);
        CHECK(pf.fc == FC_CMD);
        CHECK(pf.field_a == 0x0000);
        CHECK(pf.field_b == 1);
        CHECK(pf.frame_len == 10);
        CHECK(pf.payload != nullptr);
        CHECK(pf.payload_len == 1);
        CHECK(pf.payload != nullptr && pf.payload[0] == 0x18);
    }

    // --- parse_frame: fc=0x06 ACK response (9 bytes, no payload) ------------
    {
        ParsedFrame pf;
        ParseResult r = parse_frame(kAck, sizeof(kAck), pf);
        CHECK(r == ParseResult::OK);
        CHECK(pf.dir == DIR_RESPONSE);
        CHECK(pf.fc == FC_CMD);
        CHECK(pf.field_a == 0x0000);
        CHECK(pf.field_b == 1);
        CHECK(pf.frame_len == 9);
        CHECK(pf.payload == nullptr);
        CHECK(pf.payload_len == 0);
    }

    // --- a bad checksum on a write frame is rejected -----------------------
    {
        uint8_t bad[10];
        std::memcpy(bad, kWrite, sizeof(bad));
        bad[9] ^= 0xFF;   // corrupt the checksum
        ParsedFrame pf;
        CHECK(parse_frame(bad, sizeof(bad), pf) == ParseResult::BAD_CHECKSUM);
    }

    // --- round-trip: encode then parse -------------------------------------
    {
        uint8_t buf[16];
        const uint8_t data = 0x0C;   // 12 °C
        size_t n = encode_command(buf, sizeof(buf), 0x0000, 1, &data);
        CHECK(n == 10);
        ParsedFrame pf;
        CHECK(parse_frame(buf, n, pf) == ParseResult::OK);
        CHECK(pf.payload != nullptr && pf.payload[0] == 0x0C);
    }

    if (g_failures == 0) {
        std::printf("all tuya-codec tests passed\n");
        return 0;
    }
    std::printf("%d tuya-codec test(s) FAILED\n", g_failures);
    return 1;
}
