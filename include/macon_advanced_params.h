#pragma once

// ---------------------------------------------------------------------------
// Macon advanced ("Cn") parameters — the installer-only configuration block.
//
// Source of truth: vendor doc "Appendix C - Advanced parameter setting V4
// (Cn31)".  These are READ/WRITE holding registers and are DANGEROUS: blind or
// out-of-range writes have previously put the controller into a bad/cycling
// state.  This file carries ONLY the metadata + a write-validation guardrail.
// It does NOT expose a register-write path — nothing here can actually move a
// register.  Enabling writes is deferred until each parameter's exact
// register<->Cn index (and any scaling) is confirmed on the simulator.
//
// Register mapping (reverse-engineered):
//   wire register (2000 + N)  ==  Cn parameter N        [holding window]
// Confirmed by two independent exact anchors:
//   * Cn40 = -1   (manufacturer told the owner to set this for his glycol
//                  system; live reg2040 = -1)  -> external ground truth.
//   * Cn38 = -30  (vendor-doc default; live reg2038 = -30).
// Only those two are marked index-confirmed.  Every other entry keeps
// needs_sim_confirm = true: some live middle-block values look shifted vs the
// doc defaults (likely coincidental, but unproven), and reg2013 reads 250 vs
// Cn13's 20-55 range (likely scaled).  validate_advanced_write() therefore
// REFUSES writes to any non-anchored parameter until the sim pins the index.
// Per-parameter, clearing needs_sim_confirm is the deliverable of the
// `sim-cn-register-map` task.
//
// Pure, dependency-free (no ESP-IDF / FreeRTOS): links into the controller,
// simulator and sniffer, and compiles natively for host unit tests.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

namespace arctic {

// The advanced ("Cn") parameters live in the holding window at wire register
// (2000 + Cn).  Only Cn13..Cn57 are advanced config; 2000-2012 are telemetry.
constexpr uint8_t  ADV_CN_MIN     = 13;
constexpr uint8_t  ADV_CN_MAX     = 57;
constexpr uint16_t ADV_REG_BASE   = 2000;   // reg = ADV_REG_BASE + cn

/// Wire register address that holds Cn parameter `cn`.
inline uint16_t advanced_register_address(uint8_t cn) {
    return static_cast<uint16_t>(ADV_REG_BASE + cn);
}

// ---------------------------------------------------------------------------
// Parameter metadata
// ---------------------------------------------------------------------------

struct AdvancedParam {
    uint8_t        cn;               // Cn parameter number (reg = 2000 + cn)
    const char    *name;             // Short description
    int16_t        min_val;          // Inclusive lower bound (engineering units)
    int16_t        max_val;          // Inclusive upper bound
    int16_t        default_val;      // Vendor-doc default
    const char    *unit;             // "\xC2\xB0""C", "Hz", "min", ... or nullptr
    bool           is_signed;        // Value is signed (negative allowed)
    bool           needs_sim_confirm;// Index/scale unproven -> writes refused
    const uint8_t *enum_vals;        // If non-null: value MUST be one of these
    uint8_t        enum_count;       // Number of entries in enum_vals
};

/// Result of a validated write attempt.
enum class AdvWriteResult {
    OK,                 // In range (or in enum) and index-confirmed: allowed
    UNKNOWN_PARAM,      // No such advanced parameter
    NEEDS_SIM_CONFIRM,  // Register index/scale not yet sim-confirmed: refused
    OUT_OF_RANGE,       // Value outside [min,max]
    NOT_IN_ENUM,        // Value not one of the discrete allowed values
};

/// Look up advanced-parameter metadata by Cn number. Returns nullptr if none.
const AdvancedParam *advanced_param_lookup(uint8_t cn);

/// Look up advanced-parameter metadata by wire register address (2000 + cn).
/// Returns nullptr for addresses that are not advanced parameters.
const AdvancedParam *advanced_param_for_register(uint16_t address);

/// True if `address` is a known advanced ("Cn") parameter register.
bool register_is_advanced_param(uint16_t address);

/// Guardrail: decide whether writing `value` to Cn `cn` is permitted.
/// REJECTS (never clamps): out-of-range, not-in-enum, unknown, or not-yet
/// sim-confirmed all return a non-OK result. Only returns OK when the value is
/// valid AND the register index is confirmed.
AdvWriteResult validate_advanced_write(uint8_t cn, int16_t value);

/// Human-readable name for an AdvWriteResult ("OK", "OutOfRange", ...).
const char *adv_write_result_name(AdvWriteResult r);

/// Number of advanced parameters in the table (for iteration / tests).
size_t advanced_param_count();

/// Table entry by index [0, advanced_param_count()). Returns nullptr if OOB.
const AdvancedParam *advanced_param_at(size_t index);

}  // namespace arctic
