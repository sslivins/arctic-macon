#include "macon_faults.h"

#include <cstring>

namespace arctic {

// ---------------------------------------------------------------------------
// Canonical Macon fault table.
//
// Grouped by register (2007, 2125, 2126, 2127, 2128).  Codes/labels are the
// LCD:app text discovered live 2026-07-05, cross-referenced to the Arctic
// fault catalog.  Only confirmed bits are listed; unused bits are omitted and
// decode as raw hex by the sniffer's formatter.
// ---------------------------------------------------------------------------

const MaconFaultBit MACON_FAULT_BITS[] = {
    // reg 2007 — run-state / P15 / P16 / FE / FF.
    // bit4/6/7 = nothing; bit5 (0x20) = hot-water RUN indicator (INFO).
    { 2007, 0, "P15", "Temp difference too large (PT)",     FaultSeverity::WARNING,  MaconFaultId::TempDifferenceTooLarge },
    { 2007, 1, "P16", "Outlet temp too low (PT)",           FaultSeverity::WARNING,  MaconFaultId::OutletTempTooLow },
    { 2007, 2, "FE",  "FE protection",                      FaultSeverity::FAULT,    MaconFaultId::FeProtection },
    { 2007, 3, "FF",  "FF protection",                      FaultSeverity::FAULT,    MaconFaultId::FfProtection },
    { 2007, 5, "RUN", "Hot-water run indicator",            FaultSeverity::INFO,     MaconFaultId::Unknown },

    // reg 2125 — sensor / EEPROM / comm E-codes.
    { 2125, 0, "E28", "Outdoor EEPROM error",               FaultSeverity::FAULT,    MaconFaultId::EepromError },
    { 2125, 1, "E19", "Inlet water temp sensor",            FaultSeverity::FAULT,    MaconFaultId::InletWaterSensor },
    { 2125, 2, "E18", "Outlet water temp sensor",           FaultSeverity::FAULT,    MaconFaultId::OutletWaterSensor },
    { 2125, 3, "E13", "Cool coil temp sensor",              FaultSeverity::FAULT,    MaconFaultId::CoolCoilSensor },
    { 2125, 4, "E03", "E03 protection",                     FaultSeverity::FAULT,    MaconFaultId::E03Protection },
    { 2125, 5, "E28", "Indoor EEPROM error",                FaultSeverity::FAULT,    MaconFaultId::EepromError },
    { 2125, 6, "E27", "Driver communication",               FaultSeverity::CRITICAL, MaconFaultId::DriverCommunication },
    { 2125, 7, "E21", "Controller communication",           FaultSeverity::CRITICAL, MaconFaultId::ControllerCommunication },

    // reg 2126 — sensor / comm / compressor (E + r01/r02).
    { 2126, 0, "r02", "Compressor start failure",           FaultSeverity::FAULT,    MaconFaultId::CompressorStartFailure },
    { 2126, 1, "E26", "Indoor/outdoor communication",       FaultSeverity::CRITICAL, MaconFaultId::IndoorOutdoorCommunication },
    { 2126, 2, "r01", "IPM fault",                          FaultSeverity::CRITICAL, MaconFaultId::IpmFault },
    { 2126, 4, "E01", "Discharge temp sensor",              FaultSeverity::FAULT,    MaconFaultId::DischargeSensor },
    { 2126, 5, "E09", "Suction temp sensor",                FaultSeverity::FAULT,    MaconFaultId::SuctionSensor },
    { 2126, 6, "E05", "Coil temp sensor",                   FaultSeverity::FAULT,    MaconFaultId::CoilSensor },
    { 2126, 7, "E22", "Ambient temp sensor",                FaultSeverity::FAULT,    MaconFaultId::AmbientSensor },

    // reg 2127 — electrical / power-stage (r-codes + P02/P11).
    { 2127, 1, "P19", "AC current protection",              FaultSeverity::FAULT,    MaconFaultId::AcCurrentProtection },
    { 2127, 2, "r06", "Compressor phase current",           FaultSeverity::FAULT,    MaconFaultId::CompressorPhaseCurrent },
    { 2127, 3, "r10", "AC voltage protection",              FaultSeverity::FAULT,    MaconFaultId::AcVoltageProtection },
    { 2127, 4, "r11", "DC bus voltage protection",          FaultSeverity::FAULT,    MaconFaultId::DcBusVoltageProtection },
    { 2127, 5, "r05", "IPM temperature protection",         FaultSeverity::FAULT,    MaconFaultId::IpmTemperatureProtection },
    { 2127, 6, "P11", "High discharge temp",                FaultSeverity::FAULT,    MaconFaultId::HighDischargeTemp },
    { 2127, 7, "P02", "High pressure protection",           FaultSeverity::CRITICAL, MaconFaultId::HighPressureProtection },

    // reg 2128 — refrigerant / protection P-codes.
    { 2128, 0, "P06", "Low pressure protection",            FaultSeverity::CRITICAL, MaconFaultId::LowPressureProtection },
    { 2128, 1, "P27", "Coil overheat",                      FaultSeverity::FAULT,    MaconFaultId::CoilOverheat },
    { 2128, 2, "PC",  "Ambient too high/low (PT)",          FaultSeverity::WARNING,  MaconFaultId::AmbientOutOfRange },
    { 2128, 3, "P10", "P10 protection",                     FaultSeverity::FAULT,    MaconFaultId::P10Protection },
    { 2128, 4, "P30", "Antifreeze protection",              FaultSeverity::WARNING,  MaconFaultId::AntifreezeProtection },
    { 2128, 5, "E05", "Coil temp sensor",                   FaultSeverity::FAULT,    MaconFaultId::CoilSensor },
    { 2128, 7, "P01", "Water flow protection",              FaultSeverity::CRITICAL, MaconFaultId::WaterFlowProtection },
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
static const char *kDefaultResolution = "Contact the dealer.";

// Defined later in this file; used by macon_has_fault_id below.
static uint8_t reg_value(uint16_t reg, uint8_t r2007, uint8_t r2125,
                         uint8_t r2126, uint8_t r2127, uint8_t r2128);

struct MaconFaultDesc {
    MaconFaultId id;
    const char  *resolution;
};

static const MaconFaultDesc MACON_FAULT_DESCS[] = {
    { MaconFaultId::InletWaterSensor,
      "Check the inlet water temperature sensor at the heat exchanger for a short or open circuit and correct or replace." },
    { MaconFaultId::OutletWaterSensor,
      "Check the outlet water temperature sensor at the heat exchanger for a short or open circuit and correct or replace." },
    { MaconFaultId::CoolCoilSensor,
      "Check the coil temperature sensor for a short or open circuit and correct or replace." },
    { MaconFaultId::CoilSensor,
      "Check the heat pump coil temperature sensor and wires for a short or open circuit and correct or replace sensors." },
    { MaconFaultId::DischargeSensor,
      "Check if the compressor discharge temperature sensor for short or open circuit and correct or replace." },
    { MaconFaultId::SuctionSensor,
      "Check if the compressor suction temperature sensor for short or open circuit and correct or replace." },
    { MaconFaultId::AmbientSensor,
      "Check if the ambient temperature sensor for the heat pump or its wiring has a short or open circuit and correct or replace." },
    { MaconFaultId::ControllerCommunication,
      "Check the wired controller's cable and its connections." },
    { MaconFaultId::CompressorPhaseCurrent,
      "This applies to 3-phase units where the phasing of the wires is incorrect and needs to be corrected." },
    { MaconFaultId::HighDischargeTemp,
      "1) Check water system is operating normal, look for reduction in normal water flow. 2) Check whether there was a refrigerant leak and repair. 3) Verify unit is in normal operation with proper exhaust temperature and system pressure." },
    { MaconFaultId::HighPressureProtection,
      "1) Check whether the water temperature is too high or blocked. 2) Check whether the fan blades are blocked or if evaporator fins are blocked. 3) Check whether snow or ice has built up inside the unit. 4) Check that the water tank temperature setting is not too high." },
    { MaconFaultId::LowPressureProtection,
      "1) Check whether the unit is leaking refrigerant. 2) Repair and vacuum system, then refill with exact amount of refrigerant as per nameplate." },
    { MaconFaultId::CoilOverheat,
      "Check that the fan is in good condition and that the evaporator fins are not in need of cleaning." },
    { MaconFaultId::WaterFlowProtection,
      "Flow is too low or wiring is open circuit. Check the water system, water pump, and operation of water flow switch and correct problem." },
    { MaconFaultId::TempDifferenceTooLarge,
      "1) Check if water system is operating abnormally, such as water flow is too low. 2) Verify unit is in normal operation with proper exhaust temperature and system pressure." },
    { MaconFaultId::OutletTempTooLow,
      "1) Check water system is normal and water flow is adequate. 2) Verify unit is in normal operation with proper exhaust temperature and system pressure." },
    { MaconFaultId::AmbientOutOfRange,
      "Ambient temperature is out of the allowed operating range. Wait for conditions to improve." },
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
