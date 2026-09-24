#include "macon_fields.h"

#include "macon_faults.h"
#include "tuya_codec.h"

#include <cstring>
#include <initializer_list>

namespace arctic {

namespace {
constexpr MaconField kNoField = MaconField::WaterTankTemp;  // unused for non-Number kinds
constexpr MaconFlag  kNoFlag  = MaconFlag::Fan;             // unused for non-Flag kinds

#define TEMP(name, a0, field, ver) \
    { name, { a0, nullptr }, MaconFieldKind::Number, "C", -128, 127, 1, ver, field, kNoFlag }
#define NUM(name, a0, a1, unit, lo, hi, step, ver, field) \
    { name, { a0, a1 }, MaconFieldKind::Number, unit, lo, hi, step, ver, field, kNoFlag }
#define FLAG(name, flag) \
    { name, { nullptr, nullptr }, MaconFieldKind::Flag, nullptr, 0, 1, 1, true, kNoField, flag }
}  // namespace

const MaconFieldDesc MACON_FIELDS[] = {
    TEMP("water_tank_temp",      nullptr, MaconField::WaterTankTemp,      true),
    TEMP("outlet_water_temp",    nullptr, MaconField::OutletWaterTemp,    true),
    TEMP("inlet_water_temp",     nullptr, MaconField::InletWaterTemp,     true),
    TEMP("discharge_temp",       nullptr, MaconField::DischargeTemp,      true),
    TEMP("suction_temp",         nullptr, MaconField::SuctionTemp,        true),
    TEMP("outdoor_coil_temp",    nullptr, MaconField::OutdoorCoilTemp,    true),
    TEMP("indoor_coil_temp",     nullptr, MaconField::IndoorCoilTemp,     true),
    TEMP("outdoor_ambient_temp", nullptr, MaconField::OutdoorAmbientTemp, true),
    TEMP("ipm_temp",             nullptr, MaconField::IpmTemp,            true),
    TEMP("cooling_setpoint",     nullptr, MaconField::CoolingSetpoint,    true),
    TEMP("heating_setpoint",     nullptr, MaconField::HeatingSetpoint,    false),
    TEMP("hot_water_setpoint",   nullptr, MaconField::HotWaterSetpoint,   true),
    NUM("hot_water_ceiling",   nullptr, nullptr, "C",     0,   255,   1,   true, MaconField::HotWaterCeiling),
    NUM("compressor_freq",     nullptr, nullptr, "Hz",    0,   255,   1,   true, MaconField::CompressorFreq),
    NUM("fan_speed",           nullptr, nullptr, "RPM",   0,   2550,  10,  true, MaconField::FanLevel),
    NUM("ac_voltage",          nullptr, nullptr, "V",     0,   2550,  10,  true, MaconField::AcVoltage),
    NUM("ac_current",          nullptr, nullptr, "A",     0,   255,   1,   true, MaconField::AcCurrent),
    NUM("dc_voltage",          nullptr, nullptr, "V",     0,   2550,  10,  true, MaconField::DcVoltage),
    NUM("primary_eev",  "main_eev", "primary_eev_opening", "steps", 0, 255, 1, true, MaconField::PrimaryEev),
    NUM("realtime_power",      nullptr, nullptr, "W",     0,   25500, 100, true, MaconField::RealtimePower),
    FLAG("unit_on",         MaconFlag::UnitOn),
    FLAG("fan_on",          MaconFlag::Fan),
    FLAG("pump_on",         MaconFlag::Pump),
    FLAG("cooling_on",      MaconFlag::Cooling),
    FLAG("defrost_on",      MaconFlag::Defrost),
    FLAG("compressor_icon", MaconFlag::CompressorIcon),
    { "working_mode", { nullptr, nullptr }, MaconFieldKind::WorkingMode, nullptr,
      0, 0, 0, true, kNoField, kNoFlag },
    { "operating_direction", { nullptr, nullptr }, MaconFieldKind::OperatingDirection, nullptr,
      0, 0, 0, true, kNoField, kNoFlag },
};
const size_t MACON_FIELDS_COUNT = sizeof(MACON_FIELDS) / sizeof(MACON_FIELDS[0]);

#undef TEMP
#undef NUM
#undef FLAG

const MaconFieldDesc *macon_field_find(const char *name) {
    if (name == nullptr) return nullptr;
    for (size_t i = 0; i < MACON_FIELDS_COUNT; ++i) {
        const MaconFieldDesc &d = MACON_FIELDS[i];
        if (std::strcmp(d.name, name) == 0) return &d;
        for (const char *a : d.aliases) {
            if (a && std::strcmp(a, name) == 0) return &d;
        }
    }
    return nullptr;
}

// --- enum keys ---------------------------------------------------------------

namespace {
struct ModeKey { MaconWorkingMode mode; const char *key; };
constexpr ModeKey kModeKeys[] = {
    { MaconWorkingMode::Cooling,        "cooling" },
    { MaconWorkingMode::FloorHeating,   "floor_heating" },
    { MaconWorkingMode::FanCoilHeating, "fan_coil_heating" },
    { MaconWorkingMode::HotWater,       "hot_water" },
    { MaconWorkingMode::Auto,           "auto" },
};
}  // namespace

const char *macon_working_mode_key(MaconWorkingMode mode) {
    for (const ModeKey &k : kModeKeys) if (k.mode == mode) return k.key;
    return nullptr;
}

MaconWorkingMode macon_working_mode_from_key(const char *key) {
    if (key == nullptr) return MaconWorkingMode::Unknown;
    for (const ModeKey &k : kModeKeys) if (std::strcmp(k.key, key) == 0) return k.mode;
    return MaconWorkingMode::Unknown;
}

const char *macon_direction_key(MaconMode mode) {
    switch (mode) {
        case MaconMode::Heating: return "heating";
        case MaconMode::Cooling: return "cooling";
        default:                 return nullptr;
    }
}

MaconMode macon_direction_from_key(const char *key) {
    if (key == nullptr) return MaconMode::Unknown;
    if (std::strcmp(key, "heating") == 0) return MaconMode::Heating;
    if (std::strcmp(key, "cooling") == 0) return MaconMode::Cooling;
    return MaconMode::Unknown;
}

// --- set / get ---------------------------------------------------------------

MaconSetResult macon_field_set(MaconImage &img, const MaconFieldDesc &desc,
                               int32_t value, bool strict) {
    switch (desc.kind) {
        case MaconFieldKind::Number:
            if (strict && (value < desc.min || value > desc.max ||
                           (desc.step > 1 && value % desc.step != 0))) {
                return MaconSetResult::OutOfRange;
            }
            return img.set_value(desc.field, value);
        case MaconFieldKind::Flag:
            if (strict && value != 0 && value != 1) return MaconSetResult::OutOfRange;
            img.set_flag(desc.flag, value != 0);
            return MaconSetResult::Ok;
        case MaconFieldKind::WorkingMode: {
            const MaconWorkingMode m = static_cast<MaconWorkingMode>(value);
            if (value < 0 || value > 0xFF || macon_working_mode_key(m) == nullptr) {
                return MaconSetResult::OutOfRange;
            }
            img.set_working_mode(m);
            return MaconSetResult::Ok;
        }
        case MaconFieldKind::OperatingDirection: {
            const MaconMode m = static_cast<MaconMode>(value);
            if (value < 0 || value > 0xFF || macon_direction_key(m) == nullptr) {
                return MaconSetResult::OutOfRange;
            }
            img.set_operating_direction(m);
            return MaconSetResult::Ok;
        }
    }
    return MaconSetResult::UnknownField;
}

bool macon_field_get(const MaconState &s, const MaconFieldDesc &desc, int32_t *out) {
    if (out == nullptr) return false;
    switch (desc.kind) {
        case MaconFieldKind::Flag:
            switch (desc.flag) {
                case MaconFlag::UnitOn:         *out = s.running;       return s.faults_valid;
                case MaconFlag::Fan:            *out = s.fan_on;        return true;
                case MaconFlag::Pump:           *out = s.pump_on;       return true;
                case MaconFlag::Cooling:        *out = s.cooling_on;    return s.cooling_on_valid;
                case MaconFlag::Defrost:        *out = s.defrost_on;    return true;
                case MaconFlag::CompressorIcon: *out = s.compressor_on; return true;
            }
            return false;
        case MaconFieldKind::WorkingMode:
            *out = static_cast<int32_t>(s.working_mode);
            return s.working_mode_valid;
        case MaconFieldKind::OperatingDirection:
            *out = static_cast<int32_t>(s.mode);
            return s.mode_valid;
        case MaconFieldKind::Number:
            break;
    }
    switch (desc.field) {
        case MaconField::WaterTankTemp:      *out = s.water_tank_c;       return s.water_tank_valid;
        case MaconField::OutletWaterTemp:    *out = s.outlet_c;           return s.outlet_valid;
        case MaconField::InletWaterTemp:     *out = s.inlet_c;            return s.inlet_valid;
        case MaconField::DischargeTemp:      *out = s.discharge_c;        return s.discharge_valid;
        case MaconField::SuctionTemp:        *out = s.suction_c;          return s.suction_valid;
        case MaconField::OutdoorCoilTemp:    *out = s.outdoor_coil_c;     return s.outdoor_coil_valid;
        case MaconField::IndoorCoilTemp:     *out = s.indoor_coil_c;      return s.indoor_coil_valid;
        case MaconField::OutdoorAmbientTemp: *out = s.outdoor_ambient_c;  return s.outdoor_ambient_valid;
        case MaconField::IpmTemp:            *out = s.ipm_c;              return s.ipm_valid;
        case MaconField::CompressorFreq:     *out = s.compressor_freq;    return s.compressor_freq_valid;
        case MaconField::FanLevel:           *out = s.fan_level;          return true;
        case MaconField::AcVoltage:          *out = s.ac_voltage;         return s.ac_voltage_valid;
        case MaconField::AcCurrent:          *out = s.ac_current;         return s.ac_current_valid;
        case MaconField::DcVoltage:          *out = s.dc_voltage;         return s.dc_voltage_valid;
        case MaconField::PrimaryEev:         *out = s.primary_eev;        return s.primary_eev_valid;
        case MaconField::RealtimePower:      *out = static_cast<int32_t>(s.realtime_power_w); return s.realtime_power_valid;
        case MaconField::CoolingSetpoint:    *out = s.cooling_setpoint;   return s.cooling_setpoint_valid;
        case MaconField::HeatingSetpoint:    *out = s.aux_heat_setpoint;  return s.aux_heat_setpoint_valid;
        case MaconField::HotWaterSetpoint:   *out = s.hot_water_setpoint; return s.hot_water_setpoint_valid;
        case MaconField::HotWaterCeiling:    *out = s.hot_water_ceiling;  return s.hot_water_ceiling_valid;
    }
    return false;
}

// --- fingerprints ------------------------------------------------------------

namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void byte(uint8_t b) { h ^= b; h *= 16777619u; }
    void u16(uint16_t v) { byte(static_cast<uint8_t>(v)); byte(static_cast<uint8_t>(v >> 8)); }
    void i32(int32_t v) {
        const uint32_t u = static_cast<uint32_t>(v);
        for (int i = 0; i < 4; ++i) byte(static_cast<uint8_t>(u >> (8 * i)));
    }
    void str(const char *s) {
        if (s) while (*s) byte(static_cast<uint8_t>(*s++));
        byte(0);
    }
};

// Hash every register that differs from zero in `img` (address + value), plus a
// separator, so "which bytes changed and to what" is captured.
void hash_image(Fnv &f, const MaconImage &img) {
    uint16_t base = 0;
    uint16_t buf[MaconImage::window_count()];
    const uint16_t n = img.raw(&base, buf, MaconImage::window_count());
    for (uint16_t i = 0; i < n; ++i) {
        if (buf[i] != 0) { f.u16(static_cast<uint16_t>(base + i)); f.u16(buf[i]); }
    }
    f.u16(0xFFFF);
}

}  // namespace

uint32_t macon_layout_fingerprint() {
    Fnv f;
    for (size_t w = 0; w < tuya_codec::KNOWN_WINDOWS_COUNT; ++w) {
        const tuya_codec::RegWindow &win = tuya_codec::KNOWN_WINDOWS[w];
        f.u16(win.field_a); f.u16(win.field_b); f.u16(win.reg_base); f.byte(win.prefix_len);
    }
    for (size_t i = 0; i < MACON_FIELDS_COUNT; ++i) {
        const MaconFieldDesc &d = MACON_FIELDS[i];
        f.str(d.name);
        switch (d.kind) {
            case MaconFieldKind::Number:
                // Two probes capture both the address and the wire scaling.
                for (int32_t v : { d.step, d.step * 3 }) {
                    MaconImage img;
                    macon_field_set(img, d, v);
                    hash_image(f, img);
                }
                break;
            case MaconFieldKind::Flag: {
                MaconImage img;
                macon_field_set(img, d, 1);
                hash_image(f, img);
                break;
            }
            case MaconFieldKind::WorkingMode:
                for (const ModeKey &k : kModeKeys) {
                    MaconImage img;
                    macon_field_set(img, d, static_cast<int32_t>(k.mode));
                    f.str(k.key);
                    hash_image(f, img);
                }
                break;
            case MaconFieldKind::OperatingDirection:
                for (MaconMode m : { MaconMode::Heating, MaconMode::Cooling }) {
                    MaconImage img;
                    macon_field_set(img, d, static_cast<int32_t>(m));
                    hash_image(f, img);
                }
                break;
        }
    }
    for (uint16_t id = 0; id < static_cast<uint16_t>(MaconFaultId::Count); ++id) {
        MaconFaultSiteId sites[8];
        const size_t n = macon_fault_sites_for_id(static_cast<MaconFaultId>(id), sites, 8);
        f.str(macon_code_for_fault_id(static_cast<MaconFaultId>(id)));
        for (size_t i = 0; i < n && i < 8; ++i) {
            MaconImage img;
            img.set_fault_site(sites[i], true);
            hash_image(f, img);
        }
    }
    return f.h;
}

uint32_t macon_catalog_fingerprint() {
    Fnv f;
    f.u16(MACON_SEMANTIC_API_VERSION);
    for (size_t i = 0; i < MACON_FIELDS_COUNT; ++i) {
        const MaconFieldDesc &d = MACON_FIELDS[i];
        f.str(d.name); f.str(d.aliases[0]); f.str(d.aliases[1]); f.str(d.unit);
        f.byte(static_cast<uint8_t>(d.kind));
        f.i32(d.min); f.i32(d.max); f.i32(d.step); f.byte(d.verified);
    }
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &b = MACON_FAULT_BITS[i];
        f.str(b.code); f.str(b.label);
        f.str(macon_fault_severity_name(b.severity));
        f.str(macon_fault_resolution(b.id));
    }
    return f.h;
}

}  // namespace arctic
