#include "macon_advanced_params.h"

namespace arctic {

// K-ratio parameters (AP14..AP20) are not a contiguous range: the vendor doc
// lists discrete "RS485 reading" codes. A write must equal one of these.
static const uint8_t kRatioReadings[] = {0, 1, 2, 4, 8, 12, 16, 20};

// Display metadata for the K-ratio codes, aligned 1:1 with kRatioReadings.
// The library owns the wire->meaning mapping (identity + structured numbers +
// canonical English); UIs localize `msg_id` with {arg_a=steps, arg_b=Hz} and
// fall back to `en_default`.  Sending "12" for "6-step opening each 1 Hz" is a
// vendor detail the arctic-controller must NOT know about — it lives here.
//   { wire, label, msg_id, arg_a(steps), arg_b(Hz), en_default }
static const AdvEnumOption kFreqRatioOptions[] = {
    {  0, "0",    "kratio_none",    0,  0, "No change to running frequency"        },
    {  1, "0.25", "kratio_reduce",  2,  4, "Lowers running frequency by 2 steps per 4 Hz"  },
    {  2, "0.5",  "kratio_reduce",  2,  2, "Lowers running frequency by 2 steps per 2 Hz"  },
    {  4, "1",    "kratio_reduce",  2,  1, "Lowers running frequency by 2 steps per 1 Hz"  },
    {  8, "2",    "kratio_reduce",  4,  1, "Lowers running frequency by 4 steps per 1 Hz"  },
    { 12, "3",    "kratio_reduce",  6,  1, "Lowers running frequency by 6 steps per 1 Hz"  },
    { 16, "4",    "kratio_reduce",  8,  1, "Lowers running frequency by 8 steps per 1 Hz"  },
    { 20, "5",    "kratio_reduce", 10,  1, "Lowers running frequency by 10 steps per 1 Hz" },
};
static constexpr uint8_t kFreqRatioOptionCount =
    sizeof(kFreqRatioOptions) / sizeof(kFreqRatioOptions[0]);

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
// Fields: { ap, reg, name, detail, category, min, max, default, unit, is_signed,
//           needs_sim_confirm, enum_vals, enum_count, read_only, is_trigger,
//           enum_opts, enum_opt_count, name_msg_id, detail_msg_id }
static const AdvancedParam s_advanced[] = {
    { 13, 2012, "Max Hot-Water Setpoint",
      "Highest water temperature the unit will heat to in heating / hot-water mode.",
      CAT_AUTO,     20,  55,  50, "°C",     false, false, nullptr, 0 },  // verified: user changed 50<->55, reg2012 tracked
    { 14, 2111, "Frequency Ratio K1",
      "Adjusts compressor speed when heating or making hot water with outdoor air at or below {T:-9} and inlet water at or below {T:43}.",
      CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8, false, false, kFreqRatioOptions, kFreqRatioOptionCount, "ap.freq_ratio_k1.name", "ap.freq_ratio_k1.detail" },  // verified: write wire 0x12
    { 15, 2112, "Frequency Ratio K2",
      "Adjusts compressor speed when heating or making hot water with outdoor air between {T:-9} and {T:18} and inlet water at or below {T:43}.",
      CAT_FREQ,      0,  20,   2, nullptr,  false, false, kRatioReadings, 8, false, false, kFreqRatioOptions, kFreqRatioOptionCount, "ap.freq_ratio_k2.name", "ap.freq_ratio_k2.detail" },  // verified: write wire 0x13
    { 16, 2015, "Frequency Ratio K3",
      "Adjusts compressor speed when heating or making hot water with outdoor air above {T:18} and inlet water at or below {T:43}.",
      CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8, false, false, kFreqRatioOptions, kFreqRatioOptionCount, "ap.freq_ratio_k3.name", "ap.freq_ratio_k3.detail" },  // verified: write wire 0x41
    { 17, 2016, "Frequency Ratio K4",
      "Adjusts compressor speed when heating or making hot water with outdoor air at or below {T:-9} and inlet water above {T:43}.",
      CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8, false, false, kFreqRatioOptions, kFreqRatioOptionCount, "ap.freq_ratio_k4.name", "ap.freq_ratio_k4.detail" },  // verified: write wire 0x42
    { 18, 2017, "Frequency Ratio K5",
      "Adjusts compressor speed when heating or making hot water with outdoor air between {T:-9} and {T:18} and inlet water above {T:43}.",
      CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8, false, false, kFreqRatioOptions, kFreqRatioOptionCount, "ap.freq_ratio_k5.name", "ap.freq_ratio_k5.detail" },  // verified: write wire 0x43
    { 19, 2018, "Frequency Ratio K6",
      "Adjusts compressor speed when heating or making hot water with outdoor air above {T:18} and inlet water above {T:43}.",
      CAT_FREQ,      0,  20,   1, nullptr,  false, false, kRatioReadings, 8, false, false, kFreqRatioOptions, kFreqRatioOptionCount, "ap.freq_ratio_k6.name", "ap.freq_ratio_k6.detail" },  // verified: write wire 0x44
    { 20, 2019, "Frequency Ratio K7",
      "Adjusts compressor speed in cooling mode.",
      CAT_FREQ,      0,  20,   2, nullptr,  false, false, kRatioReadings, 8, false, false, kFreqRatioOptions, kFreqRatioOptionCount, "ap.freq_ratio_k7.name", "ap.freq_ratio_k7.detail" },  // verified: wire 0x45 + read-back
    { 24, 2023, "Heating Comp-Stop Ambient",
      "Heating mode: the compressor stops once the outdoor ambient temperature rises to this value or above.",
      CAT_PROTECT,  10,  45,  28, "°C",     false, false, nullptr, 0 },  // write-verified reg2023; OEM max is 45 (doc 80 wrong)
    { 25, 2024, "Backup E-Heater Start Ambient",
      "The backup electric heater is allowed to start when the outdoor ambient temperature falls to this value.",
      CAT_PROTECT, -45,  45,  18, "°C",     true,  false, nullptr, 0 },  // write-verified reg2024
    { 26, 2025, "Quiet-Mode Freq Decrease",
      "Frequency step the compressor drops by while quiet mode is active.",
      CAT_FREQ,      1,  20,   5, "Hz",     false, false, nullptr, 0 },  // write-verified reg2025
    { 27, 2026, "Fast-Heat Freq Increase",
      "Frequency step the compressor rises by while fast-heat mode is active.",
      CAT_FREQ,      1,  20,   5, "Hz",     false, false, nullptr, 0 },  // write-verified reg2026
    { 28, 2027, "Auto-Mode Switch Wait Time",
      "Automatic mode: time to wait before switching between heating and cooling (in units of 10 minutes).",
      CAT_AUTO,      0,  99,   3, "×10min", false, false, nullptr, 0 },  // write-verified reg2027
    { 29, 2030, "Compressor Run Time Before Defrost",
      "Cumulative compressor running time that must elapse before a defrost cycle is allowed.",
      CAT_DEFROST,   0,  90,  45, "min",    false, false, nullptr, 0 },  // write-verified reg2030; doc Item29 (0~90min, def 45)
    { 30, 2031, "Coil Temp to Enter Defrost",
      "Outdoor coil temperature at or below which a defrost cycle is entered.",
      CAT_DEFROST, -20,   5,  -7, "°C",     true,  false, nullptr, 0 },  // write-verified reg2031
    { 31, 2029, "Defrost Outdoor Temp Setting",
      "Outdoor ambient temperature setting used as one of the conditions to start defrost.",
      CAT_DEFROST, -20,   5, -10, "°C",     true,  false, nullptr, 0 },  // write-verified reg2029 (permuted, before AP29/30)
    { 32, 2032, "Air-vs-Coil Diff to Enter Defrost",
      "Temperature difference between outdoor air and the coil that triggers a defrost cycle.",
      CAT_DEFROST,   0,  30,  10, "°C",     false, false, nullptr, 0 },  // write-verified reg2032
    { 33, 2033, "Extended Defrost Time",
      "Extra time added to a defrost cycle.",
      CAT_DEFROST,   0,  90,  45, "min",    false, false, nullptr, 0 },  // write-verified reg2033
    { 34, 2035, "Max Defrost Time",
      "Maximum defrost duration; the unit exits defrost once this time is reached.",
      CAT_DEFROST,   5,  45,  12, "min",    false, false, nullptr, 0 },  // write-verified reg2035 (swapped with AP35)
    { 35, 2034, "Coil Temp to Exit Defrost",
      "Coil temperature at which defrost is considered complete and the unit exits defrost.",
      CAT_DEFROST,   5,  45,   5, "°C",     false, false, nullptr, 0 },  // write-verified reg2034 (swapped with AP34)
    { 38, 2038, "Low-Ambient Protection",
      "The unit stops when the outdoor ambient falls below this temperature. The sensor floor is -30C; setting a value below -30 disables this protection, so keep it within -30..0C.",
      CAT_PROTECT, -30,   0, -30, "°C",     true,  false, nullptr, 0 },  // write-verified reg2038
    { 39, 2039, "Freq Reduce Delay After Setpoint",
      "Time after the setpoint is reached before the working frequency is reduced. Leaving this at the default (40) disables the reduction; any other value enables it.",
      CAT_PROTECT,  10, 120,  40, "min",    false, false, nullptr, 0 },  // write-verified reg2039; doc Item39 (10~120min, def 40; doc: value 40 = no reduction, any other value reduces)
    { 40, 2040, "Cooling Comp-Stop Ambient",
      "Cooling mode: the compressor stops when the outdoor ambient drops to this temperature. Use a negative value for glycol systems and a positive value for water systems; the magnitude is the ambient threshold.",
      CAT_PROTECT, -45,  45,  -1, "°C",     true,  false, nullptr, 0 },  // write-verified reg2040
    { 41, 2041, "Main EEV Superheat Control Method",
      "Superheat control method for the main expansion valve (0 = by superheat degree, 1 = by a lab-tuned chart). Read-only: the on-unit scaling is unconfirmed.",
      CAT_EEV, 0, 20, 10, nullptr, false, false, nullptr, 0, true, false },  // write-verified reg2041; doc Item41 (0~1: 0=per overheat degree, 1=per lab-testing chart). READ-ONLY: reg observed=10-13, outside doc 0~1 -> identity/scale uncertain
    { 42, 2042, "Target Superheat, Main EEV",
      "Target superheat that the main expansion valve regulates to.",
      CAT_EEV,       0,  20,  10, "°C",     false, false, nullptr, 0 },  // write-verified reg2042
    { 43, 2043, "Three-Way Valve 2 Switch Time",
      "Switching time for three-way valve 2. Any value other than 5 cancels external Cn31 control; set it back to 5 to re-enable Cn31.",
      CAT_PUMP,      1,  99,   5, "min",    false, false, nullptr, 0 },  // write-verified reg2043; doc Item43: set to any value !=5 to cancel external mode control, 5 to allow it
    { 44, 2044, "Water-Pump Mode",
      "Water-pump running mode (0 = run at intervals, 1 = follow the compressor, 2 = run continuously).",
      CAT_PUMP,      0,   2,   2, nullptr,  false, false, nullptr, 0 },  // write-verified reg2044
    { 45, 2045, "Water-Pump Run Interval",
      "Interval between water-pump runs when the pump is in interval mode.",
      CAT_PUMP,      0,  30,   5, "min",    false, false, nullptr, 0 },  // write-verified reg2045
    { 46, 2046, "Force-Pump Low-Temp Setpoint",
      "Outdoor temperature at or below which the water pump is forced to run for freeze protection.",
      CAT_PUMP,    -25,   5,  -1, "°C",     true,  false, nullptr, 0 },  // write-verified reg2046
    { 47, 2047, "Water-System Cleaning",
      "Water-system cleaning / test (1 = test pump, 2 = test pump + 3-way valve 1, 3 = test pump + 3-way valve 2). Flow protection is disabled while this runs.",
      CAT_PUMP,      0,   3,   0, nullptr,  false, false, nullptr, 0, false, true },  // write-verified reg2047; TRIGGER: self-clearing momentary command (write 1 to start)
    { 48, 2060, "Enable Manual Freq/EEV",
      "Enables manual control of compressor frequency and EEV opening (service / test use only).",
      CAT_MANUAL,    0,   1,   0, nullptr,  false, true,  nullptr, 0 },  // verified reg2060; write-locked (arms manual mode)
    { 49, 2061, "Manual Frequency",
      "Manual compressor frequency; used only when manual mode (AP48) is enabled.",
      CAT_MANUAL,    0, 120,   0, "Hz",     false, true,  nullptr, 0 },  // verified reg2061; write-locked (safety)
    { 50, 2062, "Manual Main-EEV Opening",
      "Manual main-EEV opening; used only when manual mode (AP48) is enabled.",
      CAT_MANUAL,    0, 500,   0, "steps",  false, true,  nullptr, 0 },  // verified reg2062; write-locked (16-bit width unconfirmed)
    { 51, 2064, "Manual EVI-EEV Opening",
      "Manual EVI / auxiliary-EEV opening; used only when manual mode (AP48) is enabled.",
      CAT_MANUAL,    0, 500,   0, "steps",  false, true,  nullptr, 0 },  // verified reg2064; write-locked (safety)
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

size_t advanced_enum_option_count(uint8_t ap) {
    const AdvancedParam *p = advanced_param_lookup(ap);
    return (p && p->enum_opts) ? p->enum_opt_count : 0;
}

const AdvEnumOption *advanced_enum_option_at(uint8_t ap, size_t index) {
    const AdvancedParam *p = advanced_param_lookup(ap);
    if (!p || !p->enum_opts || index >= p->enum_opt_count) return nullptr;
    return &p->enum_opts[index];
}

const AdvEnumOption *advanced_enum_option_for_wire(uint8_t ap, int16_t wire) {
    const AdvancedParam *p = advanced_param_lookup(ap);
    if (!p || !p->enum_opts) return nullptr;
    for (uint8_t i = 0; i < p->enum_opt_count; ++i) {
        if (p->enum_opts[i].wire == wire) return &p->enum_opts[i];
    }
    return nullptr;
}

size_t advanced_category_count() { return kCategoryCount; }

const char *advanced_category_at(size_t index) {
    if (index >= kCategoryCount) return nullptr;
    return kCategories[index];
}

}  // namespace arctic
