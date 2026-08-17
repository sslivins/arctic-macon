// Native unit test for MaconMaster (Layer 3 active-master runtime) using a fake
// in-memory transport, a stepping fake clock, and a recording window sink.
// Framework-free: prints failures, returns non-zero.

#include "macon_master.h"
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

// Fake transport: records writes and flushes, hands back queued response bytes
// in `chunk`-sized pieces (to exercise the streaming reader). Empty read queue
// returns 0 (a timeout).
class FakeTransport : public MaconTransport {
public:
    std::vector<uint8_t> written;
    std::vector<uint8_t> to_read;
    size_t read_pos = 0;
    size_t chunk = 64;
    int    flush_count = 0;

    int write(const uint8_t *data, size_t n) override {
        written.insert(written.end(), data, data + n);
        return static_cast<int>(n);
    }
    int read(uint8_t *buf, size_t n, int /*timeout_ms*/) override {
        size_t avail = to_read.size() - read_pos;
        if (avail == 0) return 0;
        size_t take = avail;
        if (take > n) take = n;
        if (take > chunk) take = chunk;
        std::memcpy(buf, to_read.data() + read_pos, take);
        read_pos += take;
        return static_cast<int>(take);
    }
    void flush_rx() override { ++flush_count; }
    void queue(const uint8_t *p, size_t n) { to_read.insert(to_read.end(), p, p + n); }
    void queue_byte(uint8_t b) { to_read.push_back(b); }
};

// Monotonic fake clock that advances a fixed step per call so deadline loops
// always terminate. 10 ms/step is small enough that a poll transaction still
// gets at least one read slice before its 500 ms deadline.
class FakeClock : public MaconClock {
public:
    uint32_t t = 0;
    uint32_t step = 10;
    uint32_t now_ms() override { uint32_t v = t; t += step; return v; }
};

// Records what the master fed us.
class RecordingSink : public MaconWindowSink {
public:
    int window_calls = 0;
    int observed_calls = 0;
    uint16_t last_reg_base = 0;
    std::vector<uint8_t> last_window;
    uint16_t last_a = 0xFFFF, last_b = 0xFFFF;

    void on_window(uint16_t reg_base, const uint8_t *data, size_t len) override {
        ++window_calls;
        last_reg_base = reg_base;
        last_window.assign(data, data + len);
    }
    void on_observed(uint16_t field_a, uint16_t field_b,
                     const uint8_t * /*payload*/, size_t /*payload_len*/) override {
        ++observed_calls;
        last_a = field_a;
        last_b = field_b;
    }
};

static size_t build_window_response(uint8_t *buf, size_t cap,
                                    uint16_t field_a, uint16_t field_b,
                                    const uint8_t *payload) {
    return tuya_codec::encode_response(buf, cap, tuya_codec::FC_READ,
                                       field_a, field_b, payload);
}

int main() {
    // ----------------------------------------------------------------------
    // poll_telemetry: writes the telemetry read request and feeds the decoded
    // window (reg_base 2093, 50 bytes) to the sink.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        FakeClock clk;
        RecordingSink sink;

        uint8_t payload[50];
        std::memset(payload, 0, sizeof(payload));
        payload[0] = 12;  // reg2093
        payload[2] = 38;  // reg2095
        uint8_t resp[80];
        size_t rn = build_window_response(resp, sizeof(resp), 0, 50, payload);
        // Prepend junk + trailing tag to exercise resync.
        t.queue_byte(0xAB);
        t.queue(resp, rn);
        t.queue_byte(0x14);

        MaconMaster m(t, clk, sink);
        CHECK(m.poll_telemetry() == true);

        // Request written must be the telemetry read request (fc=0x03, 0, 50).
        uint8_t want[16];
        size_t wn = tuya_codec::encode_request(want, sizeof(want),
                                               tuya_codec::FC_READ, 0, 50);
        CHECK(t.written.size() == wn);
        CHECK(std::memcmp(t.written.data(), want, wn) == 0);

        CHECK(sink.window_calls == 1);
        CHECK(sink.observed_calls == 1);
        CHECK(sink.last_reg_base == 2093);
        CHECK(sink.last_window.size() == 50);
        CHECK(sink.last_window[0] == 12);
        CHECK(sink.last_window[2] == 38);
        CHECK(sink.last_a == 0 && sink.last_b == 50);
        CHECK(t.flush_count >= 1);  // flush before the request
    }

    // ----------------------------------------------------------------------
    // poll_holding: matches the holding window (addr 50, count 58 -> reg 2000).
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        FakeClock clk;
        RecordingSink sink;

        uint8_t payload[58];
        std::memset(payload, 0, sizeof(payload));
        payload[5] = 0x7A;
        uint8_t resp[96];
        size_t rn = build_window_response(resp, sizeof(resp), 50, 58, payload);
        t.queue(resp, rn);

        MaconMaster m(t, clk, sink);
        CHECK(m.poll_holding() == true);
        CHECK(sink.last_reg_base == 2000);
        CHECK(sink.last_window.size() == 58);
        CHECK(sink.last_window[5] == 0x7A);
        CHECK(sink.last_a == 50 && sink.last_b == 58);
    }

    // ----------------------------------------------------------------------
    // poll with no response -> false, sink untouched, terminates via deadline.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;   // empty read queue
        FakeClock clk;
        RecordingSink sink;
        MaconMaster m(t, clk, sink);
        CHECK(m.poll_telemetry() == false);
        CHECK(sink.window_calls == 0);
    }

    // ----------------------------------------------------------------------
    // poll ignores an echoed request / mismatched response but still finds the
    // real one that follows.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        FakeClock clk;
        RecordingSink sink;

        // Our own echoed read request (dir=request) then the real response.
        uint8_t echo[16];
        size_t en = tuya_codec::encode_request(echo, sizeof(echo),
                                               tuya_codec::FC_READ, 0, 50);
        uint8_t payload[50];
        std::memset(payload, 0, sizeof(payload));
        payload[0] = 21;
        uint8_t resp[80];
        size_t rn = build_window_response(resp, sizeof(resp), 0, 50, payload);
        t.queue(echo, en);
        t.queue(resp, rn);

        MaconMaster m(t, clk, sink);
        CHECK(m.poll_telemetry() == true);
        CHECK(sink.last_window[0] == 21);
    }

    // ----------------------------------------------------------------------
    // preflight_bus_idle: quiet bus -> true.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;   // no traffic
        FakeClock clk;
        RecordingSink sink;
        MaconMaster m(t, clk, sink);
        CHECK(m.preflight_bus_idle(200) == true);
    }

    // ----------------------------------------------------------------------
    // preflight_bus_idle: a valid frame on the bus -> false (another master).
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        FakeClock clk;
        RecordingSink sink;
        uint8_t payload[50];
        std::memset(payload, 0, sizeof(payload));
        uint8_t resp[80];
        size_t rn = build_window_response(resp, sizeof(resp), 0, 50, payload);
        t.queue(resp, rn);
        MaconMaster m(t, clk, sink);
        CHECK(m.preflight_bus_idle(2000) == false);
    }

    // ----------------------------------------------------------------------
    // set_cooling_setpoint via the master: flushes, writes fc=0x06, returns Ok
    // on ACK.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        FakeClock clk;
        RecordingSink sink;
        uint8_t ack[16];
        size_t an = tuya_codec::encode_command_ack(ack, sizeof(ack), 0x0000, 1);
        t.queue(ack, an);

        MaconMaster m(t, clk, sink);
        CHECK(m.set_cooling_setpoint(24) == MaconResult::Ok);
        CHECK(t.flush_count >= 1);  // flush before the write

        uint8_t want[16];
        uint8_t data = 24;
        size_t wn = tuya_codec::encode_command(want, sizeof(want), 0x0000, 1, &data);
        CHECK(std::memcmp(t.written.data(), want, wn) == 0);
    }

    // ----------------------------------------------------------------------
    // Failed write flushes again to leave the bus idle.
    // ----------------------------------------------------------------------
    {
        FakeTransport t;   // no ACK -> NoResponse
        FakeClock clk;
        RecordingSink sink;
        MaconMaster m(t, clk, sink);
        CHECK(m.set_hot_water_setpoint(40) == MaconResult::NoResponse);
        CHECK(t.flush_count >= 2);  // flush before + flush on failure
    }

    // ----------------------------------------------------------------------
    // write_register delegates to MaconLink (holding reg2013 -> offset 63).
    // ----------------------------------------------------------------------
    {
        FakeTransport t;
        FakeClock clk;
        RecordingSink sink;
        uint8_t ack[16];
        size_t an = tuya_codec::encode_command_ack(ack, sizeof(ack), 63, 1);
        t.queue(ack, an);
        MaconMaster m(t, clk, sink);
        CHECK(m.write_register(2013, 45) == MaconResult::Ok);
    }

    if (g_failures) {
        std::printf("test_macon_master: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_macon_master: all checks passed\n");
    return 0;
}
