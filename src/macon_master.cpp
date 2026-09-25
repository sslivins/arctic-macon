/*
 * MaconMaster — portable active bus-master runtime. See macon_master.h.
 *
 * The logic here is a straight lift of the controller's former
 * main/tuya/macon_master.cpp internals, with every platform dependency
 * (FreeRTOS, esp_timer, esp_log, the concrete UART) removed in favour of the
 * injected MaconTransport / MaconClock / MaconWindowSink. Behaviour on the wire
 * is unchanged.
 */
#include "macon_master.h"

#include <cstring>

namespace arctic {

namespace {
// Accumulator capacity: >= echo(9) + response(67) + trailing tag + slack.
constexpr size_t ACC_CAP = 192;
}  // namespace

MaconMaster::MaconMaster(MaconTransport &transport, MaconClock &clock,
                         MaconWindowSink &sink, int response_timeout_ms,
                         int poll_txn_deadline_ms)
    : tx_(transport),
      clock_(clock),
      sink_(sink),
      link_(transport, response_timeout_ms),
      resp_timeout_ms_(response_timeout_ms),
      poll_deadline_ms_(poll_txn_deadline_ms) {}

// ---------------------------------------------------------------------------
// One request/response transaction: send an fc=0x03 read of `win`, accumulate
// the reply, and feed the decoded register bytes to the sink. Tolerates our own
// echoed request frame, unrelated frames, junk, and the trailing block-tag byte
// (all skipped by find_frame_start / parse_frame). Returns true if the matching
// response was fed.
// ---------------------------------------------------------------------------
bool MaconMaster::poll_window(const tuya_codec::RegWindow &win)
{
    const PollOutcome outcome = poll_window_once(win);
    switch (outcome) {
        case PollOutcome::Ok:
            ++poll_stats_.ok;
            poll_stats_.consecutive_failures = 0;
            return true;
        case PollOutcome::NoResponse:
            ++poll_stats_.no_response;
            break;
        case PollOutcome::TransportError:
            ++poll_stats_.transport_error;
            break;
    }
    ++poll_stats_.consecutive_failures;
    return false;
}

MaconMaster::PollOutcome MaconMaster::poll_window_once(const tuya_codec::RegWindow &win)
{
    uint8_t req[16];
    const size_t n = tuya_codec::encode_request(req, sizeof(req),
                                                tuya_codec::FC_READ,
                                                win.field_a, win.field_b);
    if (n == 0) return PollOutcome::TransportError;

    // Bus is idle here (the consumer holds its mutex and nothing else
    // transmits): drop any leftover trailing-tag / late bytes from the previous
    // transaction so they cannot desync this one.
    tx_.flush_rx();

    if (tx_.write(req, n) < static_cast<int>(n)) {
        return PollOutcome::TransportError;
    }

    uint8_t acc[ACC_CAP];
    size_t  len      = 0;
    const uint32_t deadline = clock_.now_ms() + poll_deadline_ms_;

    while ((int32_t)(deadline - clock_.now_ms()) > 0) {
        // Drain complete frames at the head of the accumulator.
        while (len >= tuya_codec::HDR_LEN) {
            const size_t start = tuya_codec::find_frame_start(acc, len);
            if (start == len) {
                // No plausible frame start: keep only a possible partial magic.
                const size_t keep = (len < tuya_codec::HDR_LEN - 1)
                                        ? len : tuya_codec::HDR_LEN - 1;
                std::memmove(acc, acc + (len - keep), keep);
                len = keep;
                break;
            }
            if (start > 0) {
                std::memmove(acc, acc + start, len - start);
                len -= start;
                continue;
            }

            tuya_codec::ParsedFrame pf;
            const tuya_codec::ParseResult r =
                tuya_codec::parse_frame(acc, len, pf);
            if (r == tuya_codec::ParseResult::TRUNCATED) {
                break;  // need more bytes
            }
            if (r == tuya_codec::ParseResult::OK) {
                const bool match = pf.dir == tuya_codec::DIR_RESPONSE &&
                                   pf.fc  == tuya_codec::FC_READ &&
                                   pf.field_a == win.field_a &&
                                   pf.field_b == win.field_b;
                if (match) {
                    if (pf.window && pf.payload &&
                        pf.payload_len > pf.window->prefix_len) {
                        sink_.on_window(
                            pf.window->reg_base,
                            pf.payload + pf.window->prefix_len,
                            pf.payload_len - pf.window->prefix_len);
                    }
                    sink_.on_observed(pf.field_a, pf.field_b,
                                      pf.payload, pf.payload_len);
                    return PollOutcome::Ok;
                }
                // Valid but unrelated (e.g. our echoed request) -> skip it.
                std::memmove(acc, acc + pf.frame_len, len - pf.frame_len);
                len -= pf.frame_len;
                continue;
            }
            if (r == tuya_codec::ParseResult::BAD_CHECKSUM) {
                ++poll_stats_.checksum_errors;
            }
            // Bad frame at head (checksum/etc.) -> drop one byte and resync.
            std::memmove(acc, acc + 1, len - 1);
            len -= 1;
        }

        if (len >= ACC_CAP) {  // overflow guard: garbage stream
            len = 0;
        }
        const int remaining = (int)(deadline - clock_.now_ms());
        if (remaining <= 0) break;
        const int got = tx_.read(acc + len, ACC_CAP - len,
                                 remaining < resp_timeout_ms_
                                     ? remaining : resp_timeout_ms_);
        if (got < 0) {
            return PollOutcome::TransportError;
        }
        len += (size_t)got;   // got==0 just means this slice timed out; loop re-checks deadline
    }

    return PollOutcome::NoResponse;
}

// ---------------------------------------------------------------------------
// Preflight: listen for `window_ms`. If ANY valid Tuya frame is seen, another
// master (the OEM controller) is driving the bus, so it is NOT safe for us to
// transmit. Returns true only if the bus stayed quiet.
// ---------------------------------------------------------------------------
bool MaconMaster::preflight_bus_idle(int window_ms)
{
    uint8_t acc[ACC_CAP];
    size_t  len = 0;
    const uint32_t deadline = clock_.now_ms() + (uint32_t)window_ms;

    while ((int32_t)(deadline - clock_.now_ms()) > 0) {
        const int got = tx_.read(acc + len, ACC_CAP - len, 100);
        if (got > 0) {
            len += (size_t)got;
            // Scan for any valid frame.
            while (len >= tuya_codec::HDR_LEN) {
                const size_t start = tuya_codec::find_frame_start(acc, len);
                if (start == len) {
                    const size_t keep = (len < tuya_codec::HDR_LEN - 1)
                                            ? len : tuya_codec::HDR_LEN - 1;
                    std::memmove(acc, acc + (len - keep), keep);
                    len = keep;
                    break;
                }
                if (start > 0) {
                    std::memmove(acc, acc + start, len - start);
                    len -= start;
                    continue;
                }
                tuya_codec::ParsedFrame pf;
                const tuya_codec::ParseResult r =
                    tuya_codec::parse_frame(acc, len, pf);
                if (r == tuya_codec::ParseResult::TRUNCATED) break;
                if (r == tuya_codec::ParseResult::OK) {
                    // Live bus traffic -> another master present. Refuse.
                    return false;
                }
                std::memmove(acc, acc + 1, len - 1);
                len -= 1;
            }
            if (len >= ACC_CAP) len = 0;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Writes
// ---------------------------------------------------------------------------
MaconResult MaconMaster::finish_write(MaconResult r)
{
    if (r != MaconResult::Ok) {
        tx_.flush_rx();  // drain a possible late/partial reply to idle
    }
    return r;
}

MaconResult MaconMaster::set_cooling_setpoint(int celsius)
{
    tx_.flush_rx();  // bus idle under the caller's mutex: drop any stale bytes
    return finish_write(link_.set_cooling_setpoint(celsius));
}

MaconResult MaconMaster::set_hot_water_setpoint(int celsius)
{
    tx_.flush_rx();
    return finish_write(link_.set_hot_water_setpoint(celsius));
}

MaconResult MaconMaster::set_working_mode(MaconWorkingMode mode)
{
    tx_.flush_rx();
    return finish_write(link_.set_working_mode(mode));
}

MaconResult MaconMaster::write_register(uint16_t register_address, uint8_t value)
{
    tx_.flush_rx();
    return finish_write(link_.write_register(register_address, value));
}

// ---------------------------------------------------------------------------
// MaconImageSink
// ---------------------------------------------------------------------------
void MaconImageSink::on_window(uint16_t reg_base, const uint8_t *data, size_t len)
{
    const MaconCoverage cov = image_.ingest_bytes(reg_base, data, len);
    coverage_.status_updated    |= cov.status_updated;
    coverage_.telemetry_updated |= cov.telemetry_updated;
    any_ = true;
}

void MaconImageSink::on_observed(uint16_t field_a, uint16_t field_b,
                                 const uint8_t *payload, size_t payload_len)
{
    // Master only polls known windows, so every observed response maps.
    catalog_.record(field_a, field_b, 1, payload, payload_len, now_ms_);
}

MaconCoverage MaconImageSink::take_coverage()
{
    const MaconCoverage out = coverage_;
    coverage_ = MaconCoverage{};
    any_ = false;
    return out;
}

}  // namespace arctic
