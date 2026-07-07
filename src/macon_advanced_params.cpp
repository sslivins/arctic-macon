#include "macon_advanced_params.h"

namespace arctic {

// K-ratio parameters (Cn14..Cn20) are not a contiguous range: the vendor doc
// lists discrete "RS485 reading" codes. A write must equal one of these.
static const uint8_t kRatioReadings[] = {0, 1, 2, 4, 8, 12, 16, 20};

// clang-format off
// Advanced ("Cn") parameter table. Source: vendor "Appendix C - Advanced
// parameter setting V4 (Cn31)". reg = 2000 + cn.
//
// needs_sim_confirm is FALSE only for the two externally-anchored parameters
// (Cn38, Cn40). Everything else stays TRUE until the simulator pins the exact
// register index / scale — validate_advanced_write() refuses those writes.
//
// Fields: { cn, name, min, max, default, unit, is_signed, needs_sim_confirm,
//           enum_vals, enum_count }
static const AdvancedParam s_advanced[] = {
    { 13, "Max hot-water setpoint (heating)",            20,  55,  50, "°C",     false, true,  nullptr, 0 },  // reg2013 reads 250 -> likely scaled
    { 14, "Freq ratio K1 (amb<=-9, inlet<=43)",           0,  20,   1, nullptr,  false, true,  kRatioReadings, 8 },
    { 15, "Freq ratio K2 (-9<amb<=18, inlet<=43)",        0,  20,   2, nullptr,  false, true,  kRatioReadings, 8 },
    { 16, "Freq ratio K3 (amb>18, inlet<=43)",            0,  20,   1, nullptr,  false, true,  kRatioReadings, 8 },
    { 17, "Freq ratio K4 (amb<=-9, inlet>43)",            0,  20,   1, nullptr,  false, true,  kRatioReadings, 8 },
    { 18, "Freq ratio K5 (-9<amb<=18, inlet>43)",         0,  20,   1, nullptr,  false, true,  kRatioReadings, 8 },
    { 19, "Freq ratio K6 (amb>18, inlet>43)",             0,  20,   1, nullptr,  false, true,  kRatioReadings, 8 },
    { 20, "Freq ratio K7 (cooling)",                      0,  20,   2, nullptr,  false, true,  kRatioReadings, 8 },
    { 24, "Heating comp-stop ambient (amb>=Cn24 stop)",  10,  80,  28, "°C",     false, true,  nullptr, 0 },
    { 25, "Backup e-heater start ambient",              -45,  45,  18, "°C",     true,  true,  nullptr, 0 },
    { 26, "Freq decrease, quiet mode",                    1,  20,   5, "Hz",     false, true,  nullptr, 0 },
    { 27, "Freq increase, fast-heat mode",                1,  20,   5, "Hz",     false, true,  nullptr, 0 },
    { 28, "Auto-mode switch wait time N",                 0,  99,   3, "×10min", false, true,  nullptr, 0 },
    { 29, "Compressor cumulative run time",               0,  90,  45, "min",    false, true,  nullptr, 0 },
    { 30, "Outdoor coil temp to enter defrost",         -20,   5,  -7, "°C",     true,  true,  nullptr, 0 },
    { 31, "Defrost outdoor temp setting",               -20,   5, -10, "°C",     true,  true,  nullptr, 0 },
    { 32, "Outdoor-vs-coil temp diff to enter defrost",   0,  30,  10, "°C",     false, true,  nullptr, 0 },
    { 33, "Extended defrost time",                        0,  90,  45, "min",    false, true,  nullptr, 0 },
    { 34, "Max defrost time (exit condition)",            5,  45,   8, "min",    false, true,  nullptr, 0 },
    { 35, "External coil temp to exit defrost",           5,  45,  13, "°C",     false, true,  nullptr, 0 },
    { 38, "Low-ambient temp protection",                -30,   0, -30, "°C",     true,  false, nullptr, 0 },  // ANCHOR: default -30 == live reg2038
    { 39, "Reduce freq at setpoint (0=off)",              0, 120,  40, "min",    false, true,  nullptr, 0 },
    { 40, "Cooling comp-stop ambient (glycol=neg)",     -45,  45,  15, "°C",     true,  false, nullptr, 0 },  // ANCHOR: owner-set -1 == live reg2040
    { 41, "Main EEV control method",                      0,   1,   0, nullptr,  false, true,  nullptr, 0 },
    { 42, "Target superheat, main EEV",                   0,  20,   5, "°C",     false, true,  nullptr, 0 },
    { 43, "3-way valve 2 switch time (!=5 cancels Cn31)", 1,  99,   5, "min",    false, true,  nullptr, 0 },
    { 44, "Water-pump mode (heat/cool)",                  0,   2,   2, nullptr,  false, true,  nullptr, 0 },
    { 45, "Water-pump run-time interval",                 0,  30,   5, "min",    false, true,  nullptr, 0 },
    { 46, "Force-pump low-temp setpoint",               -25,   5, -10, "°C",     true,  true,  nullptr, 0 },
    { 47, "Water-system cleaning function",               0,   3,   0, nullptr,  false, true,  nullptr, 0 },
    { 48, "Enable manual freq/EEV opening",               0,   1,   0, nullptr,  false, true,  nullptr, 0 },
    { 49, "Manual frequency",                             0, 120,   0, "Hz",     false, true,  nullptr, 0 },  // range unverified
    { 50, "Manual main-EEV opening",                      0, 500,   0, "steps",  false, true,  nullptr, 0 },  // range unverified
    { 51, "Manual EVI-EEV opening",                       0, 500,   0, "steps",  false, true,  nullptr, 0 },  // range unverified
};
// clang-format on

static const size_t s_advanced_count = sizeof(s_advanced) / sizeof(s_advanced[0]);

const AdvancedParam *advanced_param_lookup(uint8_t cn) {
    for (const AdvancedParam &p : s_advanced) {
        if (p.cn == cn) return &p;
    }
    return nullptr;
}

const AdvancedParam *advanced_param_for_register(uint16_t address) {
    if (address < ADV_REG_BASE) return nullptr;
    uint16_t cn = static_cast<uint16_t>(address - ADV_REG_BASE);
    if (cn > 0xFF) return nullptr;
    return advanced_param_lookup(static_cast<uint8_t>(cn));
}

bool register_is_advanced_param(uint16_t address) {
    return advanced_param_for_register(address) != nullptr;
}

AdvWriteResult validate_advanced_write(uint8_t cn, int16_t value) {
    const AdvancedParam *p = advanced_param_lookup(cn);
    if (!p) return AdvWriteResult::UNKNOWN_PARAM;

    // Value sanity first (most actionable feedback), then the sim-confirm gate.
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

    if (p->needs_sim_confirm) return AdvWriteResult::NEEDS_SIM_CONFIRM;
    return AdvWriteResult::OK;
}

const char *adv_write_result_name(AdvWriteResult r) {
    switch (r) {
        case AdvWriteResult::OK:                return "OK";
        case AdvWriteResult::UNKNOWN_PARAM:     return "UnknownParam";
        case AdvWriteResult::NEEDS_SIM_CONFIRM: return "NeedsSimConfirm";
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

}  // namespace arctic
