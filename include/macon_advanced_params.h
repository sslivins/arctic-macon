#pragma once

// ---------------------------------------------------------------------------
// Macon advanced ("AP") parameters — the installer-only configuration block.
//
// Source of truth: vendor doc "Appendix C - Advanced parameter setting V4
// (AP31)".  These are READ/WRITE holding registers and are DANGEROUS: blind or
// out-of-range writes have previously put the controller into a bad/cycling
// state.  This file carries the metadata, the confirmed AP->register map, a
// write-validation guardrail, and a *pure* write-plan builder.  It performs NO
// bus IO itself — the consumer (controller) drives the actual Modbus write.
//
// Register mapping (reverse-engineered, EMPIRICAL):
//   There is NO formula.  `reg = 2000 + AP` is WRONG — the wire layout is
//   irregular/permuted and some params live in the telemetry window or in the
//   un-polled gap (reg 2058-2092).  A per-parameter `reg` field carries the
//   ONLY registers we trust: those confirmed by CHANGE-AND-CAPTURE (we watched
//   the OEM controller write the register, or changed the value and read the
//   exact register back).  Confirmed set (change-and-capture verified):
//     AP13->2012, AP14->2111, AP15->2112, AP16->2015, AP17->2016, AP18->2017,
//     AP19->2018, AP20->2019, AP24->2023, AP25->2024, AP26->2025, AP27->2026,
//     AP28->2027, AP29->2030, AP30->2031, AP31->2029, AP32->2032, AP33->2033,
//     AP34->2035, AP35->2034, AP38->2038, AP39->2039, AP40->2040, AP41->2041,
//     AP42->2042, AP43->2043, AP44->2044, AP45->2045, AP46->2046, AP47->2047,
//     AP48->2060, AP49->2061, AP50->2062, AP51->2064.
//   The AP24-47 block is IRREGULAR/PERMUTED (e.g. AP31->2029 and AP34<->AP35
//   are swapped, AP29->2030) — there is NO offset formula; every address above
//   was individually write-verified on the OEM bus.  AP36/37 are doc-omitted
//   and remain ADV_REG_UNKNOWN.  Correlation- or offset-inference-only mappings
//   are NOT trusted (the old "AP40->2040 / AP38->2038" anchors were disproven
//   when AP48 change-captured to reg2060, not reg2048).
//
// Write gating: a write reaches OK only when the value is valid AND the reg is
// known AND needs_sim_confirm is false.  needs_sim_confirm stays true for the
// manual-override block (AP48-51) — the register is known but writing it arms
// manual freq/EEV control (safety) and AP50's 16-bit width is unconfirmed.
//
// Pure, dependency-free (no ESP-IDF / FreeRTOS): links into the controller,
// simulator and sniffer, and compiles natively for host unit tests.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

namespace arctic {

// The advanced ("AP") parameters historically were assumed to live at wire
// register (2000 + AP).  That formula is WRONG (see file header).  Each param
// now carries an explicit, empirically-confirmed `reg`; unverified params use
// ADV_REG_UNKNOWN.  ADV_REG_BASE is kept only as the telemetry/config boundary.
constexpr uint8_t  ADV_AP_MIN       = 13;
constexpr uint8_t  ADV_AP_MAX       = 57;
constexpr uint16_t ADV_REG_BASE     = 2000;   // config window starts here
constexpr uint16_t ADV_REG_UNKNOWN  = 0;      // reg not yet change-and-captured

/// Wire register that holds AP parameter `ap`, or ADV_REG_UNKNOWN (0) if the
/// parameter is unknown or its register has not been change-and-capture
/// verified.  There is NO 2000+ap formula — this is a table lookup.
uint16_t advanced_register_address(uint8_t ap);

// ---------------------------------------------------------------------------
// Parameter metadata
// ---------------------------------------------------------------------------

struct AdvancedParam {
    uint8_t        ap;               // AP parameter number
    uint16_t       reg;             // Confirmed wire register, or ADV_REG_UNKNOWN (0)
    const char    *name;             // Short description
    const char    *category;         // Display grouping (see advanced_category_at)
    int16_t        min_val;          // Inclusive lower bound (engineering units)
    int16_t        max_val;          // Inclusive upper bound
    int16_t        default_val;      // Vendor-doc default
    const char    *unit;             // "\xC2\xB0""C", "Hz", "min", ... or nullptr
    bool           is_signed;        // Value is signed (negative allowed)
    bool           needs_sim_confirm;// reg unknown OR write-locked -> writes refused
    const uint8_t *enum_vals;        // If non-null: value MUST be one of these
    uint8_t        enum_count;       // Number of entries in enum_vals
    bool           read_only;        // Reg known but purpose/behaviour unclear:
                                     // display live value, refuse writes (READ_ONLY)
    bool           is_trigger;       // Momentary/self-clearing command register:
                                     // UI renders a button that writes the trigger
                                     // value (does not latch a stored value)
};

/// Result of a validated write attempt / plan.
enum class AdvWriteResult {
    OK,                 // Valid, register known, and writable: allowed
    UNKNOWN_PARAM,      // No such advanced parameter
    REG_UNKNOWN,        // Register not yet change-and-capture verified: refused
    NEEDS_SIM_CONFIRM,  // Register known but write-locked (safety/unverified width)
    READ_ONLY,          // Register known but flagged read-only (purpose unclear): refused
    OUT_OF_RANGE,       // Value outside [min,max]
    NOT_IN_ENUM,        // Value not one of the discrete allowed values
};

/// Look up advanced-parameter metadata by AP number. Returns nullptr if none.
const AdvancedParam *advanced_param_lookup(uint8_t ap);

/// Look up advanced-parameter metadata by wire register address (2000 + ap).
/// Returns nullptr for addresses that are not advanced parameters.
const AdvancedParam *advanced_param_for_register(uint16_t address);

/// True if `address` is a known advanced ("AP") parameter register.
bool register_is_advanced_param(uint16_t address);

/// Guardrail: decide whether writing `value` to AP `ap` is permitted.
/// REJECTS (never clamps): out-of-range, not-in-enum, unknown param, unknown
/// register, or write-locked all return a non-OK result. Only returns OK when
/// the value is valid AND the register is known AND writing is unlocked.
AdvWriteResult validate_advanced_write(uint8_t ap, int16_t value);

/// True if AP `ap` has a change-and-capture verified register (reg != 0).
bool advanced_param_reg_known(uint8_t ap);

/// A concrete, validated Modbus write: the exact register and 16-bit wire value
/// to send.  Only populated when advanced_prepare_write() returns OK.
struct AdvWritePlan {
    uint16_t reg;    // Wire register to write (fc=0x06)
    uint16_t raw;    // 16-bit value to place on the wire
};

/// Build a validated write plan for AP `ap` = `value`.  Runs the full
/// validate_advanced_write() guardrail; on OK, fills `*out` with the confirmed
/// register and the wire-encoded value.  Signed params are encoded as 8-bit
/// two's-complement in the low byte (high byte 0), matching how the mainboard
/// stores them (e.g. -10 -> 0x00F6); unsigned/enum codes are written verbatim.
/// The library performs NO IO — the caller issues the actual bus write.  `out`
/// may be null (acts as a pure validity check).  Returns the same result as
/// validate_advanced_write().
AdvWriteResult advanced_prepare_write(uint8_t ap, int16_t value, AdvWritePlan *out);

/// Decode a raw 16-bit register value into an engineering value for AP `ap`.
/// For is_signed params the low byte is sign-extended from 8 bits (e.g. raw
/// 0x00F6 -> -10); all other params are returned as-is.  Inverse of the
/// encoding in advanced_prepare_write().
int16_t advanced_decode_raw(uint8_t ap, uint16_t raw);

/// Human-readable name for an AdvWriteResult ("OK", "OutOfRange", ...).
const char *adv_write_result_name(AdvWriteResult r);

/// Number of advanced parameters in the table (for iteration / tests).
size_t advanced_param_count();

/// Table entry by index [0, advanced_param_count()). Returns nullptr if OOB.
const AdvancedParam *advanced_param_at(size_t index);

// ---------------------------------------------------------------------------
// Categories — canonical display grouping/ordering, shared by every consumer
// (controller UI, sniffer UI) so both render the advanced block identically.
// Every AdvancedParam::category equals one of these strings.
// ---------------------------------------------------------------------------

/// Number of distinct categories in canonical display order.
size_t advanced_category_count();

/// Category name at `index` in canonical display order. nullptr if OOB.
const char *advanced_category_at(size_t index);

}  // namespace arctic
