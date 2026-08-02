#include "macon_advanced_params.h"

namespace arctic {

// K-ratio parameters (AP14..AP20) are not a contiguous range: the vendor doc
// lists discrete "RS485 reading" codes. A write must equal one of these.
static const uint8_t kRatioReadings[] = {0, 1, 2, 4, 8, 12, 16, 20};

// Category names. Canonical display order is defined by kCategories below; the
// controller and sniffer UIs both render sections in that order so the advanced
// block looks identical everywhere. Keep these string literals in sync with
// kCategories.
static constexpr const char *CAT_EEV      = "EEV";
static constexpr const char *CAT_FREQ     = "Frequency";
static constexpr const char *CAT_DEFROST  = "Defrost";
static constexpr const char *CAT_PROTECT  = "Protection";
static constexpr const char *CAT_AUTO     = "Auto Mode";
static constexpr const char *CAT_PUMP     = "Pump & Valve";
static constexpr const char *CAT_MANUAL   = "Manual/Test";

// clang-format off
// Advanced ("AP") parameter table. Source: vendor "Appendix C - Advanced
// parameter setting V4 (AP31)".
//
// `reg` is the wire register, populated ONLY where confirmed by change-and-
// capture (we watched the OEM write it, or changed it and read the exact reg
// back).  Everything else is ADV_REG_UNKNOWN (0) and awaits on-site
// verification — the old "reg = 2000 + ap" / AP38/AP40 anchors were DISPROVEN
// (AP48 change-captured to reg2060, not reg2048).
//
// needs_sim_confirm is FALSE (writable) for confirmed-register, safe config
// params (AP13-20 and the write-verified AP24-46 tuning block).  It stays TRUE
// for the manual-override block (AP48-51: reg known but writing arms manual
// freq/EEV control, and AP50's 16-bit width is unconfirmed).  Two extra flags
// refine known-register params: read_only (AP41 - write-verified reg but its
// meaning/scale is still unclear, so refuse writes) and is_trigger (AP47 - a
// self-clearing momentary command register the UI renders as a button).
// validate_advanced_write() refuses REG_UNKNOWN, READ_ONLY and write-locked.
//
// Fields: { ap, reg, name, category, min, max, default, unit, is_signed,
//           needs_sim_confirm, enum_vals, enum_count, read_only, is_trigger }
static const AdvancedParam s_advanced[] = {
    { 13, 2012, "Max hot-water setpoint (heating)",           CAT_AUTO,     20,  55,  50, "°C",     false, false, nullptr, 0 },  // verified: user changed 50<->55, reg2012 tracked
    { 14, 2111, "Freq ratio K1 (amb<=-9, inlet<=43)",         CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8 },  // verified: write wire 0x12
    { 15, 2112, "Freq ratio K2 (-9<amb<=18, inlet<=43)",      CAT_FREQ,      0,  20,   2, nullptr,  false, false, kRatioReadings, 8 },  // verified: write wire 0x13
    { 16, 2015, "Freq ratio K3 (amb>18, inlet<=43)",          CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8 },  // verified: write wire 0x41
    { 17, 2016, "Freq ratio K4 (amb<=-9, inlet>43)",          CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8 },  // verified: write wire 0x42
    { 18, 2017, "Freq ratio K5 (-9<amb<=18, inlet>43)",       CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8 },  // verified: write wire 0x43
    { 19, 2018, "Freq ratio K6 (amb>18, inlet>43)",           CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8 },  // verified: write wire 0x44
    { 20, 2019, "Freq ratio K7 (cooling)",                    CAT_FREQ,      0,  20,   2, nullptr,  false, false, kRatioReadings, 8 },  // verified: wire 0x45 + read-back
    { 24, 2023, "Heating comp-stop ambient (amb>=AP24 stop)", CAT_PROTECT,  10,  45,  28, "°C",     false, false, nullptr, 0 },  // write-verified reg2023; OEM max is 45 (doc 80 wrong)
    { 25, 2024, "Backup e-heater start ambient",              CAT_PROTECT, -45,  45,  18, "°C",     true,  false, nullptr, 0 },  // write-verified reg2024
    { 26, 2025, "Freq decrease, quiet mode",                  CAT_FREQ,      1,  20,   5, "Hz",     false, false, nullptr, 0 },  // write-verified reg2025
    { 27, 2026, "Freq increase, fast-heat mode",              CAT_FREQ,      1,  20,   5, "Hz",     false, false, nullptr, 0 },  // write-verified reg2026
    { 28, 2027, "Auto-mode switch wait time N",               CAT_AUTO,      0,  99,   3, "×10min", false, false, nullptr, 0 },  // write-verified reg2027
    { 29, 2030, "Compressor cumulative running time",         CAT_DEFROST,   0,  90,  45, "min",    false, false, nullptr, 0 },  // write-verified reg2030; doc Item29 (0~90min, def 45)
    { 30, 2031, "Outdoor coil temp to enter defrost",         CAT_DEFROST, -20,   5,  -7, "°C",     true,  false, nullptr, 0 },  // write-verified reg2031
    { 31, 2029, "Defrost outdoor temp setting",               CAT_DEFROST, -20,   5, -10, "°C",     true,  false, nullptr, 0 },  // write-verified reg2029 (permuted, before AP29/30)
    { 32, 2032, "Outdoor-vs-coil temp diff to enter defrost", CAT_DEFROST,   0,  30,  10, "°C",     false, false, nullptr, 0 },  // write-verified reg2032
    { 33, 2033, "Extended defrost time",                      CAT_DEFROST,   0,  90,  45, "min",    false, false, nullptr, 0 },  // write-verified reg2033
    { 34, 2035, "Max defrost time (exit condition)",          CAT_DEFROST,   5,  45,  12, "min",    false, false, nullptr, 0 },  // write-verified reg2035 (swapped with AP35)
    { 35, 2034, "External coil temp to exit defrost",         CAT_DEFROST,   5,  45,   5, "°C",     false, false, nullptr, 0 },  // write-verified reg2034 (swapped with AP34)
    { 38, 2038, "Low-ambient temp protection",                CAT_PROTECT, -30,   0, -30, "°C",     true,  false, nullptr, 0 },  // write-verified reg2038
    { 39, 2039, "Reduce working freq when setpoint reached",  CAT_PROTECT,  10, 120,  40, "min",    false, false, nullptr, 0 },  // write-verified reg2039; doc Item39 (10~120min, def 40; doc: value 40 = no reduction, any other value reduces)
    { 40, 2040, "Cooling comp-stop ambient (glycol=neg)",     CAT_PROTECT, -45,  45,  -1, "°C",     true,  false, nullptr, 0 },  // write-verified reg2040
    { 41, 2041, "Main expansion valve overheat degree control method", CAT_EEV, 0, 20, 10, nullptr, false, false, nullptr, 0, true, false },  // write-verified reg2041; doc Item41 (0~1: 0=per overheat degree, 1=per lab-testing chart). READ-ONLY: reg observed=10-13, outside doc 0~1 -> identity/scale uncertain
    { 42, 2042, "Target superheat, main EEV",                 CAT_EEV,       0,  20,  10, "°C",     false, false, nullptr, 0 },  // write-verified reg2042
    { 43, 2043, "Switch time of three-way valve 2",           CAT_PUMP,      1,  99,   5, "min",    false, false, nullptr, 0 },  // write-verified reg2043; doc Item43: set to any value !=5 to cancel external mode control, 5 to allow it
    { 44, 2044, "Water-pump mode (heat/cool)",                CAT_PUMP,      0,   2,   2, nullptr,  false, false, nullptr, 0 },  // write-verified reg2044
    { 45, 2045, "Water-pump run-time interval",               CAT_PUMP,      0,  30,   5, "min",    false, false, nullptr, 0 },  // write-verified reg2045
    { 46, 2046, "Force-pump low-temp setpoint",               CAT_PUMP,    -25,   5,  -1, "°C",     true,  false, nullptr, 0 },  // write-verified reg2046
    { 47, 2047, "Water-system cleaning function",             CAT_PUMP,      0,   3,   0, nullptr,  false, false, nullptr, 0, false, true },  // write-verified reg2047; TRIGGER: self-clearing momentary command (write 1 to start)
    { 48, 2060, "Enable manual freq/EEV opening",             CAT_MANUAL,    0,   1,   0, nullptr,  false, true,  nullptr, 0 },  // verified reg2060; write-locked (arms manual mode)
    { 49, 2061, "Manual frequency",                           CAT_MANUAL,    0, 120,   0, "Hz",     false, true,  nullptr, 0 },  // verified reg2061; write-locked (safety)
    { 50, 2062, "Manual main-EEV opening",                    CAT_MANUAL,    0, 500,   0, "steps",  false, true,  nullptr, 0 },  // verified reg2062; write-locked (16-bit width unconfirmed)
    { 51, 2064, "Manual EVI-EEV opening",                     CAT_MANUAL,    0, 500,   0, "steps",  false, true,  nullptr, 0 },  // verified reg2064; write-locked (safety)
};
// clang-format on

// Canonical display order for advanced-param categories. Every s_advanced[].
// category points at one of these literals.
static const char *const kCategories[] = {
    CAT_EEV, CAT_FREQ, CAT_DEFROST, CAT_PROTECT, CAT_AUTO, CAT_PUMP, CAT_MANUAL,
};
static const size_t kCategoryCount = sizeof(kCategories) / sizeof(kCategories[0]);

static const size_t s_advanced_count = sizeof(s_advanced) / sizeof(s_advanced[0]);

const AdvancedParam *advanced_param_lookup(uint8_t ap) {
    for (const AdvancedParam &p : s_advanced) {
        if (p.ap == ap) return &p;
    }
    return nullptr;
}

uint16_t advanced_register_address(uint8_t ap) {
    const AdvancedParam *p = advanced_param_lookup(ap);
    return p ? p->reg : ADV_REG_UNKNOWN;
}

bool advanced_param_reg_known(uint8_t ap) {
    const AdvancedParam *p = advanced_param_lookup(ap);
    return p && p->reg != ADV_REG_UNKNOWN;
}

const AdvancedParam *advanced_param_for_register(uint16_t address) {
    if (address == ADV_REG_UNKNOWN) return nullptr;  // 0 is the "unknown" sentinel
    for (const AdvancedParam &p : s_advanced) {
        if (p.reg == address) return &p;
    }
    return nullptr;
}

bool register_is_advanced_param(uint16_t address) {
    return advanced_param_for_register(address) != nullptr;
}

AdvWriteResult validate_advanced_write(uint8_t ap, int16_t value) {
    const AdvancedParam *p = advanced_param_lookup(ap);
    if (!p) return AdvWriteResult::UNKNOWN_PARAM;

    // Value sanity first (most actionable feedback), then the write gates.
    if (p->enum_vals) {
        bool member = false;
        for (uint8_t i = 0; i < p->enum_count; ++i) {
            if (static_cast<int16_t>(p->enum_vals[i]) == value) {
                member = true;
                break;
            }
        }
        if (!member) return AdvWriteResult::NOT_IN_ENUM;
    } else if (value < p->min_val || value > p->max_val) {
        return AdvWriteResult::OUT_OF_RANGE;
    }

    // Register must be change-and-capture verified before we dare write it.
    if (p->reg == ADV_REG_UNKNOWN) return AdvWriteResult::REG_UNKNOWN;
    // Register known but flagged read-only (purpose/behaviour unclear).
    if (p->read_only) return AdvWriteResult::READ_ONLY;
    // Register known but explicitly write-locked (safety / unverified width).
    if (p->needs_sim_confirm) return AdvWriteResult::NEEDS_SIM_CONFIRM;
    return AdvWriteResult::OK;
}

AdvWriteResult advanced_prepare_write(uint8_t ap, int16_t value, AdvWritePlan *out) {
    AdvWriteResult r = validate_advanced_write(ap, value);
    if (r != AdvWriteResult::OK) return r;
    if (out) {
        const AdvancedParam *p = advanced_param_lookup(ap);
        out->reg = p->reg;
        // Signed AP params are stored as 8-bit two's-complement in the LOW BYTE
        // of the 16-bit register (high byte 0), verified on the live bus:
        // reg2040 (AP40=-1)=0x00FF, reg2038 (AP38=-30)=0x00E2, reg2029
        // (AP31=-10)=0x00F6.  So -10 must be written as 0x00F6, NOT 0xFFF6.
        // Every is_signed param is within -45..45, so one byte is sufficient.
        // Unsigned params (incl. 16-bit AP50) keep their full-width value.
        if (p->is_signed) {
            out->raw = static_cast<uint16_t>(value & 0x00FF);
        } else {
            out->raw = static_cast<uint16_t>(value);
        }
    }
    return AdvWriteResult::OK;
}

int16_t advanced_decode_raw(uint8_t ap, uint16_t raw) {
    const AdvancedParam *p = advanced_param_lookup(ap);
    if (p && p->is_signed) {
        // 8-bit two's-complement stored in the low byte (see advanced_prepare_write).
        return static_cast<int16_t>(static_cast<int8_t>(raw & 0xFF));
    }
    return static_cast<int16_t>(raw);
}

const char *adv_write_result_name(AdvWriteResult r) {
    switch (r) {
        case AdvWriteResult::OK:                return "OK";
        case AdvWriteResult::UNKNOWN_PARAM:     return "UnknownParam";
        case AdvWriteResult::REG_UNKNOWN:       return "RegUnknown";
        case AdvWriteResult::NEEDS_SIM_CONFIRM: return "NeedsSimConfirm";
        case AdvWriteResult::READ_ONLY:         return "ReadOnly";
        case AdvWriteResult::OUT_OF_RANGE:      return "OutOfRange";
        case AdvWriteResult::NOT_IN_ENUM:       return "NotInEnum";
    }
    return "?";
}

size_t advanced_param_count() { return s_advanced_count; }

const AdvancedParam *advanced_param_at(size_t index) {
    if (index >= s_advanced_count) return nullptr;
    return &s_advanced[index];
}

size_t advanced_category_count() { return kCategoryCount; }

const char *advanced_category_at(size_t index) {
    if (index >= kCategoryCount) return nullptr;
    return kCategories[index];
}

}  // namespace arctic
