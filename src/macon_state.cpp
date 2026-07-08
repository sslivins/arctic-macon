#include "macon_state.h"
#include "macon_registers.h"

#include <cstring>

namespace arctic {

MaconMode decode_mode(uint16_t raw) {
    // Wire register is a single byte; mask so a zero-extended uint16 or a stray
    // high byte can't slip past the bit test.
    const uint8_t b = static_cast<uint8_t>(raw & 0xFF);
    if (b & static_cast<uint8_t>(~MODE_KNOWN_MASK)) {
        return MaconMode::Unknown;   // an unexpected bit is set -> don't trust it
    }
    return (b & MODE_COOL_BIT) ? MaconMode::Cooling : MaconMode::Heating;
}

const char *mode_name(MaconMode m) {
    switch (m) {
        case MaconMode::Heating: return "Heating";
        case MaconMode::Cooling: return "Cooling";
        default:                 return "Unknown";
    }
}

// Interpret a stored register value as a signed whole-°C byte.
static inline int16_t s8(uint16_t v) {
    return static_cast<int16_t>(static_cast<int8_t>(v & 0xFF));
}

DecodeStatus decode_state(uint16_t base, const uint16_t *regs, size_t count,
                          MaconState *out) {
    DecodeStatus status = {0, 0};
    if (out == nullptr) {
        return status;
    }
    std::memset(out, 0, sizeof(*out));
    out->mode = MaconMode::Unknown;

    if (regs == nullptr || count == 0) {
        return status;
    }

    auto in_range = [&](uint16_t regnum) -> bool {
        const int32_t idx = static_cast<int32_t>(regnum) - static_cast<int32_t>(base);
        return idx >= 0 && idx < static_cast<int32_t>(count);
    };
    auto R = [&](uint16_t regnum) -> uint16_t {
        const int32_t idx = static_cast<int32_t>(regnum) - static_cast<int32_t>(base);
        if (idx < 0 || idx >= static_cast<int32_t>(count)) return 0;
        return regs[idx];
    };
    // Read a register, tracking presence and the expected/present tallies.
    auto val = [&](uint16_t regnum, bool *valid) -> uint16_t {
        status.registers_expected++;
        const bool present = in_range(regnum);
        if (present) status.registers_present++;
        if (valid) *valid = present;
        return R(regnum);
    };

    // --- operating mode (reg2049) -----------------------------------------
    out->mode = decode_mode(val(REG_OPERATING_MODE, &out->mode_valid));
    if (!out->mode_valid) {
        out->mode = MaconMode::Unknown;   // absent register must not decode as Heating
    }

    // --- run-state + icon bits --------------------------------------------
    // reg2007 = run/fault bitfield (0x20 = running); reg2130 = icon bitfield #1
    // (bit2 compressor, bit3 pump); reg2129 = icon bitfield #2 (bit1 defrost,
    // bit4 fan). ON follows the mainboard run-state, NOT reg2130 (the real unit
    // runs with reg2130 = 0). 0x20 is the only confirmed run code on this DHW
    // controller.
    bool run_valid = false, icon1_valid = false, icon2_valid = false, fan_valid = false;
    const uint16_t run_state  = val(REG_FAULT_RUNSTATE, &run_valid);   // reg2007
    const uint16_t icon_bits1 = val(REG_STATUS_BYTE, &icon1_valid);    // reg2130
    const uint16_t icon_bits2 = val(REG_ICON_BITS2, &icon2_valid);     // reg2129
    const uint16_t fan_raw    = val(REG_DC_MOTOR_SPEED, &fan_valid);   // reg2003 A10

    out->running       = (run_state == 0x20);
    out->compressor_on = (icon_bits1 & 0x04) != 0;  // reg2130 bit2
    out->pump_on       = (icon_bits1 & 0x08) != 0;  // reg2130 bit3
    out->defrost_on    = (icon_bits2 & 0x02) != 0;  // reg2129 bit1
    out->fan_on        = (icon_bits2 & 0x10) != 0;  // reg2129 bit4
    out->fan_level     = fan_raw;

    // --- temperatures (signed whole °C) -----------------------------------
    out->water_tank_c      = s8(val(REG_WATER_TANK_TEMP, &out->water_tank_valid));       // reg2008 o1
    out->outlet_c          = s8(val(REG_OUTLET_WATER_TEMP, &out->outlet_valid));         // reg2132 o3
    out->inlet_c           = s8(val(REG_INLET_WATER_TEMP, &out->inlet_valid));           // reg2133 o2
    out->outdoor_ambient_c = s8(val(REG_OUTDOOR_AMBIENT_TEMP, &out->outdoor_ambient_valid)); // reg2134 o4
    out->indoor_coil_c     = s8(val(REG_COOL_COIL_TEMP, &out->indoor_coil_valid));       // reg2135 A6
    out->ipm_c             = s8(val(REG_IPM_TEMP, &out->ipm_valid));                     // reg2113 A8
    // Refrigerant-cycle temps: discharge(2138) > cool coil(2135) > suction(2137)
    // > coil(2136).
    out->discharge_c       = s8(val(REG_DISCHARGE_TEMP, &out->discharge_valid));         // reg2138 A1
    out->suction_c         = s8(val(REG_SUCTION_TEMP, &out->suction_valid));             // reg2137 A3
    out->outdoor_coil_c    = s8(val(REG_COIL_TEMP, &out->outdoor_coil_valid));           // reg2136 A2

    // --- setpoint ----------------------------------------------------------
    out->hot_water_setpoint =
        static_cast<int16_t>(val(REG_HOT_WATER_SETPOINT, &out->hot_water_setpoint_valid)); // reg2012

    // --- electrical --------------------------------------------------------
    out->ac_current      = val(REG_AC_CURRENT, &out->ac_current_valid);   // reg2000 A4
    out->ac_voltage      = static_cast<uint16_t>(
        val(REG_AC_VOLTAGE, &out->ac_voltage_valid) * 10);               // reg2101 A13 (x10 => volts)
    out->dc_voltage      = static_cast<uint16_t>(
        val(REG_DC_BUS_VOLTAGE, &out->dc_voltage_valid) * 10);           // reg2001 A7 (x10 => volts)
    out->primary_eev     = val(REG_MAIN_EEV, &out->primary_eev_valid);   // reg2140 A5
    out->compressor_freq = val(REG_COMPRESSOR_FREQ, &out->compressor_freq_valid); // reg2141 A14
    out->realtime_power_w = static_cast<uint32_t>(
        val(REG_REALTIME_POWER, &out->realtime_power_valid) * 100);      // reg2114 A9 (x100 => W)

    // --- raw fault/run registers (bit decode stays in the consumer) --------
    bool f_run_v = false, f_ee_v = false, f_comp_v = false, f_elec_v = false, f_ref_v = false;
    out->fault_run  = static_cast<uint8_t>(val(REG_FAULT_RUNSTATE, &f_run_v));    // reg2007
    out->fault_ee   = static_cast<uint8_t>(val(REG_FAULT_SENSOR_EE, &f_ee_v));    // reg2125
    out->fault_comp = static_cast<uint8_t>(val(REG_FAULT_SENSOR_COMP, &f_comp_v));// reg2126
    out->fault_elec = static_cast<uint8_t>(val(REG_FAULT_ELEC, &f_elec_v));       // reg2127
    out->fault_ref  = static_cast<uint8_t>(val(REG_FAULT, &f_ref_v));             // reg2128
    out->faults_valid = f_run_v && f_ee_v && f_comp_v && f_elec_v && f_ref_v;

    return status;
}

MaconOperation decode_operation(const MaconState &s) {
    // No status/telemetry registers decoded -> nothing to say.
    if (!s.faults_valid && !s.mode_valid && !s.compressor_freq_valid) {
        return MaconOperation::Unknown;
    }

    // Fault takes priority over everything else: the four sensor/protection
    // bitfields (reg2125-2128) plus the fault bits of the run-state byte
    // (reg2007 low nibble; bit5 0x20 is the enable flag, not a fault).
    const bool fault =
        (s.faults_valid && (s.fault_ee | s.fault_comp | s.fault_elec | s.fault_ref)) ||
        (s.fault_run & 0x0F);
    if (fault) {
        return MaconOperation::Fault;
    }

    // Enable flag: reg2007 bit5. Masked (not == 0x20) so a co-set bit can't hide
    // the enabled state.
    if (!(s.fault_run & 0x20)) {
        return MaconOperation::Off;
    }

    if (s.defrost_on) {
        return MaconOperation::Defrost;
    }

    // Compressor-running gate: reg2141 frequency, NOT the reg2130 icon bit
    // (which reads 0 on this unit even while the compressor runs).
    if (!(s.compressor_freq_valid && s.compressor_freq > 0)) {
        return MaconOperation::Idle;
    }

    // Running: report the reversing-valve direction. An Unknown/untrusted mode
    // falls back to Heating (this is a heating-biased unit).
    return (s.mode == MaconMode::Cooling) ? MaconOperation::Cooling
                                          : MaconOperation::Heating;
}

const char *operation_name(MaconOperation op) {
    switch (op) {
        case MaconOperation::Off:     return "Off";
        case MaconOperation::Fault:   return "Fault";
        case MaconOperation::Idle:    return "Idle";
        case MaconOperation::Defrost: return "Defrost";
        case MaconOperation::Cooling: return "Cooling";
        case MaconOperation::Heating: return "Heating";
        default:                      return "Unknown";
    }
}

}  // namespace arctic
