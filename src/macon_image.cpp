#include "macon_image.h"

#include "macon_registers.h"

#include <cstring>
#include <initializer_list>

namespace arctic {

// The MaconImage window bounds are declared as plain literals in the public
// header so that header carries no register-map dependency. Keep them honest:
static_assert(MaconImage::window_base() == HOLDING_START,
              "MaconImage window base drifted from HOLDING_START");
static_assert(MaconImage::window_count() == INPUT_START + INPUT_COUNT - HOLDING_START,
              "MaconImage window count drifted from the register map");

// ---------------------------------------------------------------------------
// Field -> wire mapping. Kind determines the inverse scaling / range applied
// when a semantic value is stored into the one-byte register.
// ---------------------------------------------------------------------------
namespace {

enum class Kind : uint8_t {
    SignedTemp,   // whole °C, stored as signed int8 low byte
    Raw,          // 0..255, no scaling
    DivTen,       // volts stored as raw = value / 10
    DivHundred,   // watts stored as raw = value / 100
};

struct FieldMap { uint16_t reg; Kind kind; };

bool field_map(MaconField f, FieldMap *m) {
    switch (f) {
        case MaconField::WaterTankTemp:      *m = { REG_WATER_TANK_TEMP,      Kind::SignedTemp }; return true;
        case MaconField::OutletWaterTemp:    *m = { REG_OUTLET_WATER_TEMP,    Kind::SignedTemp }; return true;
        case MaconField::InletWaterTemp:     *m = { REG_INLET_WATER_TEMP,     Kind::SignedTemp }; return true;
        case MaconField::DischargeTemp:      *m = { REG_DISCHARGE_TEMP,       Kind::SignedTemp }; return true;
        case MaconField::SuctionTemp:        *m = { REG_SUCTION_TEMP,         Kind::SignedTemp }; return true;
        case MaconField::OutdoorCoilTemp:    *m = { REG_COIL_TEMP,            Kind::SignedTemp }; return true;
        case MaconField::IndoorCoilTemp:     *m = { REG_COOL_COIL_TEMP,       Kind::SignedTemp }; return true;
        case MaconField::OutdoorAmbientTemp: *m = { REG_OUTDOOR_AMBIENT_TEMP, Kind::SignedTemp }; return true;
        case MaconField::IpmTemp:            *m = { REG_IPM_TEMP,             Kind::SignedTemp }; return true;
        case MaconField::CompressorFreq:     *m = { REG_COMPRESSOR_FREQ,      Kind::Raw };        return true;
        case MaconField::FanLevel:           *m = { REG_DC_MOTOR_SPEED,       Kind::DivTen };     return true;
        case MaconField::AcVoltage:          *m = { REG_AC_VOLTAGE,           Kind::DivTen };     return true;
        case MaconField::AcCurrent:          *m = { REG_AC_CURRENT,           Kind::Raw };        return true;
        case MaconField::DcVoltage:          *m = { REG_DC_BUS_VOLTAGE,       Kind::DivTen };     return true;
        case MaconField::PrimaryEev:         *m = { REG_MAIN_EEV,             Kind::Raw };        return true;
        case MaconField::RealtimePower:      *m = { REG_REALTIME_POWER,       Kind::DivHundred }; return true;
        case MaconField::CoolingSetpoint:    *m = { REG_COOLING_SETPOINT,     Kind::SignedTemp }; return true;
        case MaconField::HeatingSetpoint:    *m = { REG_AUX_HEAT_SETPOINT,    Kind::SignedTemp }; return true;
        case MaconField::HotWaterSetpoint:   *m = { REG_HOT_WATER_SETPOINT,   Kind::SignedTemp }; return true;
        case MaconField::HotWaterCeiling:    *m = { REG_HOT_WATER_CEILING,    Kind::Raw };        return true;
    }
    return false;
}

// Register + mask for each run-state flag.
void flag_map(MaconFlag f, uint16_t *reg, uint8_t *mask) {
    switch (f) {
        case MaconFlag::Fan:     *reg = REG_ICON_BITS2;    *mask = 0x10; return;  // reg2129 bit4
        case MaconFlag::Cooling: *reg = REG_ICON_BITS2;    *mask = 0x04; return;  // reg2129 bit2
        case MaconFlag::Pump:    *reg = REG_STATUS_BYTE;   *mask = 0x08; return;  // reg2130 bit3
        case MaconFlag::UnitOn:  *reg = REG_FAULT_RUNSTATE; *mask = 0x20; return; // reg2007 bit5
    }
    *reg = 0; *mask = 0;
}

}  // namespace

// ---------------------------------------------------------------------------

uint16_t macon_field_address(MaconField f) {
    FieldMap m;
    return field_map(f, &m) ? m.reg : 0;
}

void MaconImage::clear() {
    std::memset(values_, 0, sizeof(values_));
    std::memset(present_, 0, sizeof(present_));
}

int MaconImage::index_of(uint16_t addr) const {
    if (!in_window(addr)) return -1;
    return static_cast<int>(addr - kBase);
}

void MaconImage::put(uint16_t addr, uint16_t value) {
    const int idx = index_of(addr);
    if (idx < 0) return;
    values_[idx] = static_cast<uint16_t>(value & 0xFF);
    present_[idx] = true;
}

MaconCoverage MaconImage::ingest(uint16_t base, const uint16_t *regs, size_t count) {
    MaconCoverage cov = { false, false };
    if (regs == nullptr || count == 0) return cov;
    const uint32_t end = static_cast<uint32_t>(base) + static_cast<uint32_t>(count);
    cov.status_updated    = base <= REG_OPERATING_MODE  && end > REG_OPERATING_MODE;
    cov.telemetry_updated = base <= REG_COMPRESSOR_FREQ && end > REG_COMPRESSOR_FREQ;
    for (size_t i = 0; i < count; ++i) {
        put(static_cast<uint16_t>(base + i), regs[i]);
    }
    return cov;
}

MaconCoverage MaconImage::ingest_bytes(uint16_t base, const uint8_t *regs, size_t count) {
    MaconCoverage cov = { false, false };
    if (regs == nullptr || count == 0) return cov;
    const uint32_t end = static_cast<uint32_t>(base) + static_cast<uint32_t>(count);
    cov.status_updated    = base <= REG_OPERATING_MODE  && end > REG_OPERATING_MODE;
    cov.telemetry_updated = base <= REG_COMPRESSOR_FREQ && end > REG_COMPRESSOR_FREQ;
    for (size_t i = 0; i < count; ++i) {
        put(static_cast<uint16_t>(base + i), regs[i]);
    }
    return cov;
}

MaconSetResult MaconImage::set_temp(MaconField f, int celsius) {
    FieldMap m;
    if (!field_map(f, &m) || m.kind != Kind::SignedTemp) return MaconSetResult::UnknownField;
    const bool clamped = celsius < -128 || celsius > 127;
    int c = celsius < -128 ? -128 : (celsius > 127 ? 127 : celsius);
    put(m.reg, static_cast<uint16_t>(static_cast<uint8_t>(static_cast<int8_t>(c))));
    return clamped ? MaconSetResult::Truncated : MaconSetResult::Ok;
}

MaconSetResult MaconImage::set_value(MaconField f, int32_t value) {
    FieldMap m;
    if (!field_map(f, &m)) return MaconSetResult::UnknownField;

    switch (m.kind) {
        case Kind::SignedTemp: {
            const bool clamped = value < -128 || value > 127;
            int32_t c = value < -128 ? -128 : (value > 127 ? 127 : value);
            put(m.reg, static_cast<uint16_t>(static_cast<uint8_t>(static_cast<int8_t>(c))));
            return clamped ? MaconSetResult::Truncated : MaconSetResult::Ok;
        }
        case Kind::Raw: {
            const bool bad = value < 0 || value > 255;
            int32_t raw = value < 0 ? 0 : (value > 255 ? 255 : value);
            put(m.reg, static_cast<uint16_t>(raw));
            return bad ? MaconSetResult::Truncated : MaconSetResult::Ok;
        }
        case Kind::DivTen: {
            int32_t raw = value / 10;
            const bool lossy = value < 0 || (value % 10) != 0 || raw > 255;
            if (raw < 0) raw = 0; else if (raw > 255) raw = 255;
            put(m.reg, static_cast<uint16_t>(raw));
            return lossy ? MaconSetResult::Truncated : MaconSetResult::Ok;
        }
        case Kind::DivHundred: {
            int32_t raw = value / 100;
            const bool lossy = value < 0 || (value % 100) != 0 || raw > 255;
            if (raw < 0) raw = 0; else if (raw > 255) raw = 255;
            put(m.reg, static_cast<uint16_t>(raw));
            return lossy ? MaconSetResult::Truncated : MaconSetResult::Ok;
        }
    }
    return MaconSetResult::UnknownField;
}

void MaconImage::set_flag(MaconFlag f, bool on) {
    uint16_t reg = 0; uint8_t mask = 0;
    flag_map(f, &reg, &mask);
    if (reg == 0) return;
    const int idx = index_of(reg);
    if (idx < 0) return;
    uint16_t v = values_[idx];
    if (on) v |= mask; else v &= static_cast<uint16_t>(~mask);
    values_[idx] = static_cast<uint16_t>(v & 0xFF);
    present_[idx] = true;
}

void MaconImage::set_working_mode(MaconWorkingMode mode) {
    put(REG_WORKING_MODE, static_cast<uint16_t>(static_cast<uint8_t>(mode)));
}

void MaconImage::set_fault(MaconFaultId id, bool active) {
    if (id == MaconFaultId::Unknown) return;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (fb.id != id) continue;
        const int idx = index_of(fb.reg);
        if (idx < 0) continue;
        const uint16_t mask = static_cast<uint16_t>(1u << fb.bit);
        if (active) values_[idx] |= mask;
        else        values_[idx] &= static_cast<uint16_t>(~mask);
        values_[idx] = static_cast<uint16_t>(values_[idx] & 0xFF);
        present_[idx] = true;
    }
}

int MaconImage::set_fault_by_code(const char *code, bool active) {
    if (!code) return -1;
    int written = 0;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (std::strcmp(fb.code, code) != 0) continue;
        const int idx = index_of(fb.reg);
        if (idx < 0) continue;
        const uint16_t mask = static_cast<uint16_t>(1u << fb.bit);
        if (active) values_[idx] |= mask;
        else        values_[idx] &= static_cast<uint16_t>(~mask);
        values_[idx] = static_cast<uint16_t>(values_[idx] & 0xFF);
        present_[idx] = true;
        ++written;
    }
    return written;
}

void MaconImage::clear_faults() {
    // Clear the four telemetry fault registers entirely; on the run/state
    // register keep the RUN indicator bit (0x20) and clear only the fault bits.
    for (uint16_t reg : { REG_FAULT_RUNSTATE, REG_FAULT_SENSOR_EE,
                          REG_FAULT_SENSOR_COMP, REG_FAULT_ELEC, REG_FAULT }) {
        const int idx = index_of(reg);
        if (idx < 0) continue;
        values_[idx] = (reg == REG_FAULT_RUNSTATE)
                           ? static_cast<uint16_t>(values_[idx] & 0x20)
                           : 0;
        present_[idx] = true;
    }
}

bool MaconImage::set_register(uint16_t addr, uint16_t value) {
    if (!in_window(addr) || value > 0xFF) return false;
    put(addr, value);
    return true;
}

bool MaconImage::get_register(uint16_t addr, uint16_t *out) const {
    const int idx = index_of(addr);
    if (idx < 0 || out == nullptr) return false;
    *out = values_[idx];
    return true;
}

uint16_t MaconImage::raw(uint16_t *base_out, uint16_t *buf, uint16_t cap) const {
    if (base_out) *base_out = kBase;
    if (buf == nullptr) return 0;
    const uint16_t n = cap < kCount ? cap : kCount;
    for (uint16_t i = 0; i < n; ++i) buf[i] = values_[i];
    return n;
}

DecodeStatus MaconImage::decode(MaconState *out) const {
    return decode_state(kBase, values_, kCount, out, present_);
}

}  // namespace arctic
