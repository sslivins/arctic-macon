#pragma once

// ---------------------------------------------------------------------------
// MaconImage — the opaque Macon register image.
//
// This is the encapsulation boundary that lets a consumer (controller /
// simulator) hold and mutate the heat pump's register state WITHOUT knowing any
// register address, bit position, or value scaling. The controller owns only a
// MaconImage handle plus semantic operations:
//
//   * ingest()                — feed a real-mode wire window (passive/active)
//   * set_temp/set_value/...  — set a field by MEANING (whole °C, volts, …)
//   * set_flag/set_fault      — set a run flag / inject a fault by identity
//   * decode()                — produce the decoded MaconState
//   * raw()/get_register()    — relay bytes for a debug dump (no interpretation)
//
// All register/bit/scaling knowledge lives here and in macon_registers.h /
// macon_faults.h. The image tracks per-register PRESENCE, so a field only
// decodes as valid once it has actually been ingested or set — a freshly
// constructed image decodes as entirely absent.
//
// Pure, dependency-free (no ESP-IDF / FreeRTOS). The consumer owns any locking:
// this type is NOT internally synchronised.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

#include "macon_state.h"
#include "macon_faults.h"

namespace arctic {

// Numeric / enum fields the consumer can set by meaning. Units match the
// corresponding MaconState field (whole °C, volts, amps, watts, Hz, raw level);
// the library applies the inverse wire scaling.
enum class MaconField : uint8_t {
    // Temperatures (whole °C) — use set_temp().
    WaterTankTemp, OutletWaterTemp, InletWaterTemp, DischargeTemp, SuctionTemp,
    OutdoorCoilTemp, IndoorCoilTemp, OutdoorAmbientTemp, IpmTemp,
    // Numeric readings / setpoints — use set_value().
    CompressorFreq,     // Hz (raw)
    FanLevel,           // raw DC motor level
    AcVoltage,          // volts   (wire = /10)
    AcCurrent,          // amps    (wire = x1)
    DcVoltage,          // volts   (wire = /10)
    PrimaryEev,         // steps   (raw)
    RealtimePower,      // watts   (wire = /100)
    CoolingSetpoint,    // whole °C
    HeatingSetpoint,    // whole °C (aux; UNVERIFIED on this unit)
    HotWaterSetpoint,   // whole °C
    HotWaterCeiling,    // whole °C (AP13 ceiling)
};

// Run-state flags (bit-level fields decoded from icon/run registers).
// CompressorIcon is the LCD compressor icon only: the controller's "compressor
// running" state follows CompressorFreq > 0, not this bit.
enum class MaconFlag : uint8_t { Fan, Cooling, Pump, UnitOn, Defrost, CompressorIcon };

// Wire address backing a settable numeric/enum field. Intended only for a
// human-facing diagnostic dump (e.g. a CSV "address" column) that wants to show
// where a value came from — the consumer still gets the value from its decoded
// state, never by reading the register itself. Returns 0 if the field is not
// backed by a single address.
uint16_t macon_field_address(MaconField f);

// Outcome of a set_temp()/set_value() call.
enum class MaconSetResult : uint8_t {
    Ok,            // value stored exactly
    Truncated,     // stored, but the wire encoding lost precision or clamped
    UnknownField,  // field not settable / not mapped
    OutOfRange,    // rejected by a strict setter; the image was NOT modified
};

// Which decode-relevant windows an ingest() touched, so the consumer can manage
// freshness without knowing register numbers.
struct MaconCoverage {
    bool status_updated;     // run-state / operating-mode window present
    bool telemetry_updated;  // telemetry window (temps / power / compressor) present
};

class MaconImage {
public:
    MaconImage() { clear(); }

    // Reset to entirely-absent (all registers zero, presence cleared).
    void clear();

    // --- real-mode ingest ---------------------------------------------------
    // Copy a wire window into the image (index = addr - base), marking each
    // covered register present. Registers outside the image window are skipped.
    MaconCoverage ingest(uint16_t base, const uint16_t *regs, size_t count);
    // Convenience for the raw 1-byte wire payload the transport delivers.
    MaconCoverage ingest_bytes(uint16_t base, const uint8_t *regs, size_t count);

    // --- semantic setters (demo / simulation) -------------------------------
    MaconSetResult set_temp(MaconField f, int celsius);
    MaconSetResult set_value(MaconField f, int32_t value);
    void set_flag(MaconFlag f, bool on);
    void set_working_mode(MaconWorkingMode mode);
    // Live operating direction (reg2049), set by the unit itself — distinct
    // from the user-selected working mode. Unknown is ignored.
    void set_operating_direction(MaconMode mode);
    void set_fault(MaconFaultId id, bool active);
    int  set_fault_by_code(const char *code, bool active); // code = HTTP data
    // Set/clear exactly one physical fault site (see macon_fault_site_id).
    // Returns false for an unknown site token.
    bool set_fault_site(MaconFaultSiteId site, bool active);
    void clear_faults();

    // --- slave-side wire I/O (simulator) ------------------------------------
    // Mark every register of every known Tuya window present (value 0), so the
    // image decodes the way a master that ingested full windows would see it.
    void fill_baseline();
    // Serve a read: copy the payload of wire window (field_a, field_b) into
    // out. Returns the payload length, or 0 for an unknown window / small cap.
    size_t read_window(uint16_t field_a, uint16_t field_b,
                       uint8_t *out, size_t cap) const;
    // Apply a master fc=0x06 write of `len` bytes at wire address `wire_addr`,
    // exactly as the real unit reflects it. Returns false (image untouched) if
    // any byte falls outside a known window.
    bool apply_write(uint16_t wire_addr, const uint8_t *data, size_t len);

    // --- raw register debug passthrough -------------------------------------
    // Address-based poke/peek for a raw-register debug endpoint. The image owns
    // the window bounds so the consumer needs no HOLDING_START/INPUT_START.
    bool set_register(uint16_t addr, uint16_t value);
    bool get_register(uint16_t addr, uint16_t *out) const;
    // Copy present-or-zero register values into buf (up to cap); returns count
    // written and reports the window base. Relays bytes without interpreting.
    uint16_t raw(uint16_t *base_out, uint16_t *buf, uint16_t cap) const;

    // --- decode -------------------------------------------------------------
    DecodeStatus decode(MaconState *out) const;

    // --- window metadata ----------------------------------------------------
    static constexpr uint16_t window_base()  { return kBase; }
    static constexpr uint16_t window_count() { return kCount; }
    bool in_window(uint16_t addr) const {
        return addr >= kBase && addr < static_cast<uint16_t>(kBase + kCount);
    }

private:
    // Flat cache spanning both the holding and telemetry windows. The bounds
    // are declared here as library-owned literals so this public header carries
    // no register-map dependency; macon_image.cpp static_asserts them against
    // macon_registers.h so they can never silently drift.
    static constexpr uint16_t kBase  = 2000;   // holding-window base
    static constexpr uint16_t kCount = 143;    // spans both windows through telemetry

    // Returns index for addr, or -1 if outside the window.
    int index_of(uint16_t addr) const;
    // Set a single register value + presence (no-op if outside window).
    void put(uint16_t addr, uint16_t value);

    uint16_t values_[kCount];
    bool     present_[kCount];
};

}  // namespace arctic
