#pragma once

// ---------------------------------------------------------------------------
// MaconMaster — active bus-master runtime logic (Layer 3, portable core).
//
// This is the pure, host-testable heart of the "Tab5 is the sole bus master"
// mode: it polls the OEM telemetry/holding register windows over fc=0x03 reads
// and issues fc=0x06 verified setpoint/register writes (via MaconLink). It owns
// NONE of the platform: no FreeRTOS task, no UART driver, no esp_timer, no
// logging. Those live in the consumer's thin shim, which:
//
//   * implements MaconTransport over its concrete RS485 UART,
//   * implements MaconClock over its monotonic timer,
//   * implements MaconWindowSink to route decoded windows into its own state,
//   * owns the poll task and the bus mutex that serialises transactions.
//
// Keeping this logic in the library means the frame-draining/resync state
// machine and the bus-idle preflight are exercised by native unit tests with a
// fake transport, and the controller carries zero Tuya/Macon wire knowledge.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

#include "macon_link.h"   // MaconTransport, MaconLink, MaconResult
#include "tuya_codec.h"   // RegWindow, KNOWN_WINDOWS

namespace arctic {

// ---------------------------------------------------------------------------
// Injected monotonic clock (consumer owns the concrete timer)
// ---------------------------------------------------------------------------
class MaconClock {
public:
    virtual ~MaconClock() = default;

    /// Free-running millisecond counter. Only differences are used, so any
    /// monotonic epoch is fine (wraparound-safe via unsigned subtraction).
    virtual uint32_t now_ms() = 0;
};

// ---------------------------------------------------------------------------
// Sink for windows decoded while polling (consumer routes them into its state)
// ---------------------------------------------------------------------------
class MaconWindowSink {
public:
    virtual ~MaconWindowSink() = default;

    /// A telemetry/holding read response was decoded. `reg_base` is the first
    /// absolute Arctic register the window carries; `data`/`len` are the raw
    /// register bytes with the window prefix already stripped.
    virtual void on_window(uint16_t reg_base, const uint8_t *data, size_t len) = 0;

    /// Raw observed window (wire addr/count + full payload) for the consumer's
    /// observed-register bookkeeping/diagnostics. Called once per matched
    /// response, alongside on_window.
    virtual void on_observed(uint16_t field_a, uint16_t field_b,
                             const uint8_t *payload, size_t payload_len) = 0;
};

// ---------------------------------------------------------------------------
// MaconMaster
// ---------------------------------------------------------------------------
class MaconMaster {
public:
    /// `transport`, `clock` and `sink` must all outlive the MaconMaster.
    /// `response_timeout_ms` bounds each transport read slice; MaconLink writes
    /// use the same bound. `poll_txn_deadline_ms` bounds a whole window read.
    MaconMaster(MaconTransport &transport, MaconClock &clock,
                MaconWindowSink &sink, int response_timeout_ms = 200,
                int poll_txn_deadline_ms = 500);

    // --- polling ------------------------------------------------------------
    /// Poll one register window: send an fc=0x03 read, drain frames tolerating
    /// our own echo / unrelated frames / junk / the trailing block-tag, and
    /// feed the matching response to the sink. Returns true iff a matching
    /// response was decoded and fed. NOT thread-safe: the consumer serialises
    /// bus access with its own mutex.
    bool poll_window(const tuya_codec::RegWindow &win);

    /// Poll the OEM holding window (regs 2000.. — electrical/status/mode).
    bool poll_holding() { return poll_window(tuya_codec::KNOWN_WINDOWS[1]); }

    /// Poll the OEM telemetry window (regs 2093.. — setpoints/temps/EEV).
    bool poll_telemetry() { return poll_window(tuya_codec::KNOWN_WINDOWS[0]); }

    // --- preflight ----------------------------------------------------------
    /// Listen (read-only, no TX) for `window_ms`. Returns true only if NO valid
    /// Tuya frame was seen — i.e. no other master is driving the bus, so it is
    /// safe to become master. Returns false the instant live traffic is seen.
    bool preflight_bus_idle(int window_ms);

    // --- writes (fc=0x06, verified) ----------------------------------------
    // Each flushes stale RX under the caller's mutex, issues the write via
    // MaconLink, and flushes again on failure to leave the bus idle. NOT
    // thread-safe: the consumer holds its bus mutex across the call.
    MaconResult set_cooling_setpoint(int celsius);
    MaconResult set_hot_water_setpoint(int celsius);
    MaconResult write_register(uint16_t register_address, uint8_t value);

private:
    MaconResult finish_write(MaconResult r);

    MaconTransport   &tx_;
    MaconClock       &clock_;
    MaconWindowSink  &sink_;
    MaconLink         link_;
    int               resp_timeout_ms_;
    int               poll_deadline_ms_;
};

}  // namespace arctic
