// Native unit test for MaconLink (Layer 2 transaction logic) using a fake
// in-memory transport. Framework-free: prints failures, returns non-zero.

#include "macon_link.h"
#include "tuya_codec.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace arctic;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// A fake transport: records everything written, and hands back queued response
// bytes on read() in `chunk`-sized pieces (to exercise the streaming reader).
// An empty read queue returns 0 (a timeout).
class FakeTransport : public MaconTransport {
public:
    std::vector<uint8_t> written;
    std::vector<uint8_t> to_read;
    size_t read_pos = 0;
    size_t chunk = 64;   // bytes handed back per read() call

    int write(const uint8_t *data, size_t n) override {
        written.insert(written.end(), data, data + n);
        return static_cast<int>(n);
    }
    int read(uint8_t *buf, size_t n, int /*timeout_ms*/) override {
        size_t avail = to_read.size() - read_pos;
        if (avail == 0) return 0;  // timeout, no data
        size_t take = avail;
        if (take > n) take = n;
        if (take > chunk) take = chunk;
        std::memcpy(buf, to_read.data() + read_pos, take);
        read_pos += take;
        return static_cast<int>(take);
    }
    void queue(const uint8_t *p, size_t n) { to_read.insert(to_read.end(), p, p + n); }
    void queue_byte(uint8_t b) { to_read.push_back(b); }
};

// Build the 50-byte telemetry response with the three setpoint bytes set.
static size_t build_telemetry(uint8_t *buf, size_t cap,
                              int8_t cooling, int8_t aux, int8_t hotwater) {
    uint8_t payload[50];
    std::memset(payload, 0, sizeof(payload));
    payload[0] = static_cast<uint8_t>(cooling);   // reg2093
    payload[1] = static_cast<uint8_t>(aux);       // reg2094
    payload[2] = static_cast<uint8_t>(hotwater);  // reg2095
    return tuya_codec::encode_response(buf, cap, tuya_codec::FC_READ,
                                       /*field_a=*/0, /*field_b=*/50, payload);
}

int main() {
    // ----------------------------------------------------------------------
    // set_cooling_setpoint: writes the exact fc=0x06 frame to wire addr 0x0000
    // and returns Ok on a matching ACK.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        uint8_t ack[16];
        size_t an = tuya_codec::encode_command_ack(ack, sizeof(ack), 0x0000, 1);
        t.queue(ack, an);
        t.queue_byte(0x14);  // trailing block-tag byte the reader must resync past

        MaconLink link(t);
        CHECK(link.set_cooling_setpoint(24) == MaconResult::Ok);

        // Written frame must be the byte-exact command frame.
        uint8_t want[16];
        uint8_t data = 24;
        size_t wn = tuya_codec::encode_command(want, sizeof(want), 0x0000, 1, &data);
        CHECK(t.written.size() == wn);
        CHECK(std::memcmp(t.written.data(), want, wn) == 0);
    }

    // ----------------------------------------------------------------------
    // set_hot_water_setpoint: wire addr 0x0002.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        uint8_t ack[16];
        size_t an = tuya_codec::encode_command_ack(ack, sizeof(ack), 0x0002, 1);
        t.queue(ack, an);

        MaconLink link(t);
        CHECK(link.set_hot_water_setpoint(38) == MaconResult::Ok);

        uint8_t want[16];
        uint8_t data = 38;
        size_t wn = tuya_codec::encode_command(want, sizeof(want), 0x0002, 1, &data);
        CHECK(t.written.size() == wn);
        CHECK(std::memcmp(t.written.data(), want, wn) == 0);
    }

    // ----------------------------------------------------------------------
    // Negative celsius is carried as a signed byte.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        uint8_t ack[16];
        size_t an = tuya_codec::encode_command_ack(ack, sizeof(ack), 0x0000, 1);
        t.queue(ack, an);

        MaconLink link(t);
        CHECK(link.set_cooling_setpoint(-5) == MaconResult::Ok);
        uint8_t want[16];
        uint8_t data = static_cast<uint8_t>(static_cast<int8_t>(-5));
        size_t wn = tuya_codec::encode_command(want, sizeof(want), 0x0000, 1, &data);
        CHECK(std::memcmp(t.written.data(), want, wn) == 0);
    }

    // ----------------------------------------------------------------------
    // read_*_setpoint: issues a telemetry read, extracts the right byte.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        uint8_t resp[64];
        size_t rn = build_telemetry(resp, sizeof(resp), /*cooling=*/12, /*aux=*/40, /*hw=*/38);
        // Prepend junk + a stray block-tag to exercise resync.
        t.queue_byte(0x00);
        t.queue_byte(0xAB);
        t.queue(resp, rn);
        t.queue_byte(0x14);

        MaconLink link(t);
        int v = -999;
        CHECK(link.read_hot_water_setpoint(&v) == MaconResult::Ok);
        CHECK(v == 38);

        // The request frame written must be the telemetry read request.
        uint8_t want[16];
        size_t wn = tuya_codec::encode_request(want, sizeof(want),
                                               tuya_codec::FC_READ, 0, 50);
        CHECK(t.written.size() == wn);
        CHECK(std::memcmp(t.written.data(), want, wn) == 0);
    }
    {
        FakeTransport t;
        uint8_t resp[64];
        size_t rn = build_telemetry(resp, sizeof(resp), /*cooling=*/-5, 40, 38);
        t.queue(resp, rn);
        MaconLink link(t);
        int v = -999;
        CHECK(link.read_cooling_setpoint(&v) == MaconResult::Ok);
        CHECK(v == -5);  // signed byte
    }
    {
        FakeTransport t;
        uint8_t resp[64];
        size_t rn = build_telemetry(resp, sizeof(resp), 12, /*aux=*/40, 38);
        t.queue(resp, rn);
        MaconLink link(t);
        int v = -999;
        CHECK(link.read_heating_setpoint(&v) == MaconResult::Ok);  // reg2094 (untested)
        CHECK(v == 40);
    }

    // ----------------------------------------------------------------------
    // No response -> NoResponse.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;   // empty read queue
        MaconLink link(t);
        CHECK(link.set_cooling_setpoint(24) == MaconResult::NoResponse);
        int v = 0;
        CHECK(link.read_cooling_setpoint(&v) == MaconResult::NoResponse);
    }

    // ----------------------------------------------------------------------
    // ACK for the wrong address is not accepted (skipped -> NoResponse).
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        uint8_t ack[16];
        size_t an = tuya_codec::encode_command_ack(ack, sizeof(ack), 0x0001, 1); // wrong addr
        t.queue(ack, an);
        MaconLink link(t);
        CHECK(link.set_cooling_setpoint(24) == MaconResult::NoResponse);
    }

    // ----------------------------------------------------------------------
    // Speculative NACK: an fc-exception frame (0x86) is reported as Nack.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        uint8_t nack[9] = {0x55, 0xAA, 0x0F, 0x86, 0x00, 0x00, 0x00, 0x01, 0x00};
        t.queue(nack, sizeof(nack));
        MaconLink link(t);
        CHECK(link.set_cooling_setpoint(24) == MaconResult::Nack);
    }

    if (g_failures) {
        std::printf("test_macon_link: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_macon_link: all checks passed\n");
    return 0;
}
