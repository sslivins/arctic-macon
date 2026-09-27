#include "macon_faults.h"

#include <cstring>

namespace arctic {

// ---------------------------------------------------------------------------
// Canonical Macon fault table.
//
// Grouped by register (2007, 2125, 2126, 2127, 2128).  Codes were discovered
// live on the real unit 2026-07-05 and re-verified bit by bit against the OEM
// wired controller + Macon app on 2026-09-26 (arctic-macon #37), which also
// found the FA (DC fan motor) bit.  Labels are our own plain-English wording of
// the app's meaning, not the app's text.  `wired_display` records whether the
// OEM wired controller shows the code (r-codes, P19 and FA are app-only).
// FE/FF wording comes from the Arctic EVI fault catalog (the app has no text
// for them). Only confirmed bits are listed; unused bits (2007.4/6/7, 2126.3, 2128.6)
// showed nothing on either and are omitted.
// ---------------------------------------------------------------------------

const MaconFaultBit MACON_FAULT_BITS[] = {
    // { reg, bit, code, English label, severity, id, label i18n key, wired_display }
    // reg 2007 — run-state / P15 / P16 / FE / FF.
    // bit4/6/7 = nothing; bit5 (0x20) = RUN indicator (INFO): clearing it makes
    // the OEM controller show the unit as off.
    { 2007, 0, "P15", "Inlet/outlet temperature difference too large",           FaultSeverity::WARNING,  MaconFaultId::TempDifferenceTooLarge,      "fault.temp_difference_too_large.label",   true  },
    { 2007, 1, "P16", "Outlet water too cold",                                   FaultSeverity::WARNING,  MaconFaultId::OutletTempTooLow,            "fault.outlet_temp_too_low.label",         true  },
    { 2007, 2, "FE",  "Pressure difference protection at startup",               FaultSeverity::FAULT,    MaconFaultId::FeProtection,                "fault.fe_protection.label",               true  },
    { 2007, 3, "FF",  "Pressure difference protection while running",            FaultSeverity::FAULT,    MaconFaultId::FfProtection,                "fault.ff_protection.label",               true  },
    { 2007, 5, "RUN", "Run indicator",                                           FaultSeverity::INFO,     MaconFaultId::Unknown,                     nullptr,                                   false },

    // reg 2125 — sensor / EEPROM / comm E-codes.
    { 2125, 0, "E28", "Outdoor board memory (EEPROM) fault",                     FaultSeverity::FAULT,    MaconFaultId::EepromError,                 "fault.eeprom_outdoor.label",              true  },
    { 2125, 1, "E19", "Inlet water temperature sensor fault",                    FaultSeverity::FAULT,    MaconFaultId::InletWaterSensor,            "fault.inlet_water_sensor.label",          true  },
    { 2125, 2, "E18", "Outlet water temperature sensor fault",                   FaultSeverity::FAULT,    MaconFaultId::OutletWaterSensor,           "fault.outlet_water_sensor.label",         true  },
    { 2125, 3, "E13", "Cold coil temperature sensor fault",                      FaultSeverity::FAULT,    MaconFaultId::CoolCoilSensor,              "fault.cold_coil_sensor.label",            true  },
    { 2125, 4, "E03", "E03 protection",                                          FaultSeverity::FAULT,    MaconFaultId::E03Protection,               "fault.e03_protection.label",              true  },
    { 2125, 5, "E28", "Indoor board memory (EEPROM) fault",                      FaultSeverity::FAULT,    MaconFaultId::EepromError,                 "fault.eeprom_indoor.label",               true  },
    { 2125, 6, "E27", "Lost communication with the compressor driver board",     FaultSeverity::CRITICAL, MaconFaultId::DriverCommunication,         "fault.driver_communication.label",        true  },
    { 2125, 7, "E21", "Lost communication with the wired controller",            FaultSeverity::CRITICAL, MaconFaultId::ControllerCommunication,     "fault.controller_communication.label",    true  },

    // reg 2126 — sensor / comm / compressor (E + r01/r02).
    { 2126, 0, "r02", "Compressor failed to start",                              FaultSeverity::FAULT,    MaconFaultId::CompressorStartFailure,      "fault.compressor_start_failure.label",    false },
    { 2126, 1, "E26", "Lost communication between indoor and outdoor boards",    FaultSeverity::CRITICAL, MaconFaultId::IndoorOutdoorCommunication,  "fault.indoor_outdoor_communication.label", true  },
    { 2126, 2, "r01", "Compressor inverter (IPM) fault",                         FaultSeverity::CRITICAL, MaconFaultId::IpmFault,                    "fault.ipm_fault.label",                   false },
    { 2126, 4, "E01", "Discharge temperature sensor fault",                      FaultSeverity::FAULT,    MaconFaultId::DischargeSensor,             "fault.discharge_sensor.label",            true  },
    { 2126, 5, "E09", "Suction temperature sensor fault",                        FaultSeverity::FAULT,    MaconFaultId::SuctionSensor,               "fault.suction_sensor.label",              true  },
    { 2126, 6, "E05", "Outdoor coil temperature sensor fault",                   FaultSeverity::FAULT,    MaconFaultId::CoilSensor,                  "fault.outdoor_coil_sensor.label",         true  },
    { 2126, 7, "E22", "Outdoor air temperature sensor fault",                    FaultSeverity::FAULT,    MaconFaultId::AmbientSensor,               "fault.ambient_sensor.label",              true  },

    // reg 2127 — electrical / power-stage (FA + r-codes + P02/P11).
    { 2127, 0, "FA",  "DC fan motor fault",                                      FaultSeverity::FAULT,    MaconFaultId::DcFanMotor,                  "fault.dc_fan_motor.label",                false },
    { 2127, 1, "P19", "Input current too high",                                  FaultSeverity::FAULT,    MaconFaultId::AcCurrentProtection,         "fault.ac_current_protection.label",       false },
    { 2127, 2, "r06", "Compressor phase current fault",                          FaultSeverity::FAULT,    MaconFaultId::CompressorPhaseCurrent,      "fault.compressor_phase_current.label",    false },
    { 2127, 3, "r10", "Supply voltage too high or too low",                      FaultSeverity::FAULT,    MaconFaultId::AcVoltageProtection,         "fault.ac_voltage_protection.label",       false },
    { 2127, 4, "r11", "Inverter DC voltage too high or too low",                 FaultSeverity::FAULT,    MaconFaultId::DcBusVoltageProtection,      "fault.dc_bus_voltage_protection.label",   false },
    { 2127, 5, "r05", "Compressor inverter (IPM) overheating",                   FaultSeverity::FAULT,    MaconFaultId::IpmTemperatureProtection,    "fault.ipm_temperature_protection.label",  false },
    { 2127, 6, "P11", "Discharge temperature too high",                          FaultSeverity::FAULT,    MaconFaultId::HighDischargeTemp,           "fault.high_discharge_temp.label",         true  },
    { 2127, 7, "P02", "Refrigerant pressure too high",                           FaultSeverity::CRITICAL, MaconFaultId::HighPressureProtection,      "fault.high_pressure.label",               true  },

    // reg 2128 — refrigerant / protection P-codes.
    { 2128, 0, "P06", "Refrigerant pressure too low",                            FaultSeverity::CRITICAL, MaconFaultId::LowPressureProtection,       "fault.low_pressure.label",                true  },
    { 2128, 1, "P27", "Cold coil too hot",                                       FaultSeverity::FAULT,    MaconFaultId::CoilOverheat,                "fault.coil_overheat.label",               true  },
    { 2128, 2, "PC",  "Outdoor temperature outside operating range",             FaultSeverity::WARNING,  MaconFaultId::AmbientOutOfRange,           "fault.ambient_out_of_range.label",        true  },
    { 2128, 3, "P10", "P10 protection",                                          FaultSeverity::FAULT,    MaconFaultId::P10Protection,               "fault.p10_protection.label",              true  },
    { 2128, 4, "P30", "Cold coil too cold (freeze protection)",                  FaultSeverity::WARNING,  MaconFaultId::AntifreezeProtection,        "fault.antifreeze_protection.label",       true  },
    { 2128, 5, "E05", "Coil temperature sensor fault",                           FaultSeverity::FAULT,    MaconFaultId::CoilSensor,                  "fault.coil_sensor.label",                 true  },
    { 2128, 7, "P01", "Water flow too low (flow switch)",                        FaultSeverity::CRITICAL, MaconFaultId::WaterFlowProtection,         "fault.water_flow.label",                  true  },
};

const size_t MACON_FAULT_BITS_COUNT = sizeof(MACON_FAULT_BITS) / sizeof(MACON_FAULT_BITS[0]);

// ---------------------------------------------------------------------------
// Per-id descriptor table (semantic identity -> resolution text).
//
// Resolution guidance moved here from the controller (heatpump_errors.cpp
// kResolutions) so it is owned once, keyed by id, and cannot drift between the
// two sites of a multi-site code. Ids without specific guidance fall back to
// kDefaultResolution.
// ---------------------------------------------------------------------------
static const char *kDefaultResolution       = "Contact your dealer.";
static const char *kDefaultResolutionMsgId  = "fault.default.resolution";

// Defined later in this file; used by macon_has_fault_id below.
static uint8_t reg_value(uint16_t reg, uint8_t r2007, uint8_t r2125,
                         uint8_t r2126, uint8_t r2127, uint8_t r2128);

struct MaconFaultDesc {
    MaconFaultId id;
    const char  *resolution_msg_id;  // stable i18n key for `resolution`
    const char  *resolution;         // English source of truth + i18n fallback
};

static const MaconFaultDesc MACON_FAULT_DESCS[] = {
    { MaconFaultId::InletWaterSensor, "fault.inlet_water_sensor.resolution",
      "Check the inlet water temperature sensor at the heat exchanger and its wiring for a short or open circuit. Repair or replace it." },
    { MaconFaultId::OutletWaterSensor, "fault.outlet_water_sensor.resolution",
      "Check the outlet water temperature sensor at the heat exchanger and its wiring for a short or open circuit. Repair or replace it." },
    { MaconFaultId::CoolCoilSensor, "fault.cold_coil_sensor.resolution",
      "Check the cold coil temperature sensor and its wiring for a short or open circuit. Repair or replace it." },
    { MaconFaultId::CoilSensor, "fault.coil_sensor.resolution",
      "Check the coil temperature sensors and their wiring for a short or open circuit. Repair or replace them." },
    { MaconFaultId::DischargeSensor, "fault.discharge_sensor.resolution",
      "Check the compressor discharge temperature sensor and its wiring for a short or open circuit. Repair or replace it." },
    { MaconFaultId::SuctionSensor, "fault.suction_sensor.resolution",
      "Check the compressor suction temperature sensor and its wiring for a short or open circuit. Repair or replace it." },
    { MaconFaultId::AmbientSensor, "fault.ambient_sensor.resolution",
      "Check the outdoor air temperature sensor and its wiring for a short or open circuit. Repair or replace it." },
    { MaconFaultId::ControllerCommunication, "fault.controller_communication.resolution",
      "Check the wired controller cable and its connections." },
    { MaconFaultId::CompressorPhaseCurrent, "fault.compressor_phase_current.resolution",
      "On three-phase units this usually means the supply phases are wired in the wrong order. Have the phase wiring corrected." },
    { MaconFaultId::HighDischargeTemp, "fault.high_discharge_temp.resolution",
      "1) Check that the water system is working normally and the water flow hasn't dropped. 2) Check for a refrigerant leak and repair it. 3) Confirm the unit runs normally with the correct discharge temperature and system pressure." },
    { MaconFaultId::HighPressureProtection, "fault.high_pressure.resolution",
      "1) Check whether the water temperature is too high or the water flow is blocked. 2) Check that the fan blades and evaporator fins aren't blocked. 3) Check for snow or ice built up inside the unit. 4) Check that the hot water setpoint isn't too high." },
    { MaconFaultId::LowPressureProtection, "fault.low_pressure.resolution",
      "1) Check the unit for a refrigerant leak. 2) Repair it, evacuate the system, then recharge it with the exact amount of refrigerant shown on the nameplate." },
    { MaconFaultId::CoilOverheat, "fault.coil_overheat.resolution",
      "Check that the fan is working properly and that the evaporator fins are clean." },
    { MaconFaultId::WaterFlowProtection, "fault.water_flow.resolution",
      "The water flow is too low or the flow switch wiring is open. Check the water system, the water pump and the flow switch, and fix the problem." },
    { MaconFaultId::TempDifferenceTooLarge, "fault.temp_difference_too_large.resolution",
      "1) Check whether the water system is working abnormally, for example the water flow is too low. 2) Confirm the unit runs normally with the correct discharge temperature and system pressure." },
    { MaconFaultId::OutletTempTooLow, "fault.outlet_temp_too_low.resolution",
      "1) Check that the water system is working normally and the water flow is adequate. 2) Confirm the unit runs normally with the correct discharge temperature and system pressure." },
    { MaconFaultId::AmbientOutOfRange, "fault.ambient_out_of_range.resolution",
      "The outdoor temperature is outside the unit's operating range. The unit resumes on its own once conditions return to normal." },
    { MaconFaultId::DcFanMotor, "fault.dc_fan_motor.resolution",
      "Check the outdoor fan motor, its wiring and connector, and that the fan isn't blocked by snow or ice." },
};
static const size_t MACON_FAULT_DESCS_COUNT =
    sizeof(MACON_FAULT_DESCS) / sizeof(MACON_FAULT_DESCS[0]);

// First table row carrying `id` (representative code/label/severity), or null.
static const MaconFaultBit *first_bit_for_id(MaconFaultId id)
{
    if (id == MaconFaultId::Unknown) return nullptr;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        if (MACON_FAULT_BITS[i].id == id) return &MACON_FAULT_BITS[i];
    }
    return nullptr;
}

MaconFaultSiteId macon_fault_site_id(uint16_t reg, uint8_t bit)
{
    if (!macon_fault_bit(reg, bit)) return 0;
    return static_cast<MaconFaultSiteId>((reg << 3) | (bit & 0x7));
}

const MaconFaultBit *macon_fault_bit_for_site(MaconFaultSiteId site)
{
    if (site == 0) return nullptr;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (macon_fault_site_id(fb.reg, fb.bit) == site) return &fb;
    }
    return nullptr;
}

MaconFaultId macon_fault_id_from_code(const char *code)
{
    if (!code) return MaconFaultId::Unknown;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        if (std::strcmp(MACON_FAULT_BITS[i].code, code) == 0) {
            return MACON_FAULT_BITS[i].id;
        }
    }
    return MaconFaultId::Unknown;
}

const char *macon_code_for_fault_id(MaconFaultId id)
{
    const MaconFaultBit *fb = first_bit_for_id(id);
    return fb ? fb->code : nullptr;
}

const char *macon_label_for_fault_id(MaconFaultId id)
{
    const MaconFaultBit *fb = first_bit_for_id(id);
    return fb ? fb->label : nullptr;
}

FaultSeverity macon_severity_for_fault_id(MaconFaultId id)
{
    const MaconFaultBit *fb = first_bit_for_id(id);
    return fb ? fb->severity : FaultSeverity::INFO;
}

const char *macon_fault_resolution(MaconFaultId id)
{
    for (size_t i = 0; i < MACON_FAULT_DESCS_COUNT; ++i) {
        if (MACON_FAULT_DESCS[i].id == id) return MACON_FAULT_DESCS[i].resolution;
    }
    return kDefaultResolution;
}

const char *macon_fault_resolution_msg_id(MaconFaultId id)
{
    for (size_t i = 0; i < MACON_FAULT_DESCS_COUNT; ++i) {
        if (MACON_FAULT_DESCS[i].id == id) return MACON_FAULT_DESCS[i].resolution_msg_id;
    }
    return kDefaultResolutionMsgId;
}

bool macon_has_fault_id(uint8_t r2007, uint8_t r2125, uint8_t r2126,
                        uint8_t r2127, uint8_t r2128, MaconFaultId id)
{
    if (id == MaconFaultId::Unknown) return false;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (fb.id != id || fb.severity == FaultSeverity::INFO) continue;
        uint8_t v = reg_value(fb.reg, r2007, r2125, r2126, r2127, r2128);
        if (v & (1u << fb.bit)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------

size_t macon_fault_sites_for_id(MaconFaultId id, MaconFaultSiteId *out, size_t max)
{
    if (id == MaconFaultId::Unknown) return 0;
    size_t n = 0;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (fb.id != id || fb.severity == FaultSeverity::INFO) continue;
        if (out && n < max) out[n] = macon_fault_site_id(fb.reg, fb.bit);
        ++n;
    }
    return n;
}

const char *macon_fault_severity_name(FaultSeverity severity)
{
    switch (severity) {
        case FaultSeverity::INFO:     return "info";
        case FaultSeverity::WARNING:  return "warning";
        case FaultSeverity::FAULT:    return "error";
        case FaultSeverity::CRITICAL: return "critical";
    }
    return "unknown";
}

const MaconFaultBit *macon_fault_bits_for_reg(uint16_t reg, size_t *count)
{
    const MaconFaultBit *first = nullptr;
    size_t n = 0;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        if (MACON_FAULT_BITS[i].reg == reg) {
            if (!first) first = &MACON_FAULT_BITS[i];
            ++n;
        }
    }
    if (count) *count = n;
    return n ? first : nullptr;
}

const MaconFaultBit *macon_fault_bit(uint16_t reg, uint8_t bit)
{
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        if (MACON_FAULT_BITS[i].reg == reg && MACON_FAULT_BITS[i].bit == bit) {
            return &MACON_FAULT_BITS[i];
        }
    }
    return nullptr;
}

size_t macon_fault_bits_for_code(const char *code, const MaconFaultBit **out,
                                 size_t max)
{
    if (!code) return 0;
    size_t n = 0;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        if (std::strcmp(MACON_FAULT_BITS[i].code, code) == 0) {
            if (out && n < max) out[n] = &MACON_FAULT_BITS[i];
            ++n;
        }
    }
    return n;
}

int macon_set_fault_by_code(uint16_t *regs, uint16_t base, size_t count,
                            const char *code, bool on)
{
    if (!regs || !code) return -1;
    int written = 0;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (std::strcmp(fb.code, code) != 0) continue;
        if (fb.reg < base) continue;
        size_t idx = static_cast<size_t>(fb.reg - base);
        if (idx >= count) continue;
        uint16_t mask = static_cast<uint16_t>(1u << fb.bit);
        if (on) regs[idx] |= mask;
        else    regs[idx] &= static_cast<uint16_t>(~mask);
        ++written;
    }
    return written;
}

static uint8_t reg_value(uint16_t reg, uint8_t r2007, uint8_t r2125,
                         uint8_t r2126, uint8_t r2127, uint8_t r2128)
{
    switch (reg) {
        case 2007: return r2007;
        case 2125: return r2125;
        case 2126: return r2126;
        case 2127: return r2127;
        case 2128: return r2128;
        default:   return 0;
    }
}

bool macon_has_fault(uint8_t r2007, uint8_t r2125, uint8_t r2126,
                     uint8_t r2127, uint8_t r2128)
{
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (fb.severity == FaultSeverity::INFO) continue;
        uint8_t v = reg_value(fb.reg, r2007, r2125, r2126, r2127, r2128);
        if (v & (1u << fb.bit)) return true;
    }
    return false;
}

size_t macon_decode_faults(uint8_t r2007, uint8_t r2125, uint8_t r2126,
                           uint8_t r2127, uint8_t r2128,
                           MaconFault *out, size_t max)
{
    if (!out || max == 0) return 0;

    size_t n = 0;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT && n < max; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (fb.severity == FaultSeverity::INFO) continue;   // skip RUN indicator
        uint8_t v = reg_value(fb.reg, r2007, r2125, r2126, r2127, r2128);
        if (!(v & (1u << fb.bit))) continue;
        out[n].code     = fb.code;
        out[n].label    = fb.label;
        out[n].severity = fb.severity;
        out[n].reg      = fb.reg;
        out[n].bit      = fb.bit;
        out[n].id       = fb.id;
        out[n].site     = macon_fault_site_id(fb.reg, fb.bit);
        out[n].label_msg_id  = fb.label_msg_id;
        out[n].wired_display = fb.wired_display;
        ++n;
    }

    // Stable insertion sort by descending severity (small n; keeps table order
    // within equal severity for deterministic display).
    for (size_t i = 1; i < n; ++i) {
        MaconFault key = out[i];
        size_t j = i;
        while (j > 0 && out[j - 1].severity < key.severity) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }
    return n;
}

}  // namespace arctic
