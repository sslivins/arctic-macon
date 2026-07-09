#pragma once

#include <cstdint>
#include <cstddef>

#include "macon_faults.h"

namespace arctic {

// ---------------------------------------------------------------------------
// Register address constants  (protocol native numbering)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MACON layout (reverse-engineered from live captures on the real heat pump).
// This is NOT the legacy Arctic/ECO-600 numbering — the Macon device reuses
// the same wire register numbers for entirely different fields. Only registers
// confirmed against synchronized on-unit menu (o/A code) reads are named;
// everything else decodes as a raw byte ("Unknown").
//
// Each wire register is ONE signed/unsigned byte (see decode_byte()).
// ---------------------------------------------------------------------------

// "Holding" window (wire addr=50, base 2000). Despite the legacy "holding"
// name, on the Macon unit this block carries telemetry, not control regs.
constexpr uint16_t REG_AC_CURRENT           = 2000;  // A4  AC input current
constexpr uint16_t REG_DC_BUS_VOLTAGE       = 2001;  // A7  DC bus voltage (x10 = V)
constexpr uint16_t REG_DC_MOTOR_SPEED       = 2003;  // A10 DC (fan) motor speed
constexpr uint16_t REG_FAULT_RUNSTATE       = 2007;  // fault bitfield + run indicator
constexpr uint16_t REG_WATER_TANK_TEMP      = 2008;  // o1  water tank temp
// Cn13 "Highest setting temperature of hot water, heating mode" (20~55 °C,
// default 50). This is the CEILING/limit the mainboard enforces, NOT the live
// user setpoint (that is REG_HOT_WATER_SETPOINT=2095). Confirmed: dialing the
// display setpoint down did not move reg2012; it stayed at its 50 °C ceiling.
constexpr uint16_t REG_HOT_WATER_CEILING    = 2012;  // Cn13 max hot-water temp (ceiling, not live setpoint)

// Operating direction, set by the reversing valve (NOT a user-selectable menu
// mode). Runtime telemetry, not static config: observed flipping 0->4 live when
// the unit switched to cooling. Validated across the full 4749-row historical
// dataset — only 0x00 (heating) and 0x04 (cooling) ever appear. See macon_state.h
// for the MaconMode decode (bit-masked on 0x04, Unknown for any unobserved bit).
constexpr uint16_t REG_OPERATING_MODE       = 2049;

constexpr uint16_t HOLDING_START = 2000;
constexpr uint16_t HOLDING_COUNT = 58;   // 2000–2057

// "Telemetry" window (wire addr=0, base 2093; byte0 = reg2093 = cooling
// setpoint — the former "7-byte prefix" is NOT static, byte0 is the setpoint).
// The controller SETS a setpoint by writing one byte to the matching wire byte
// offset (fc=0x06): wire addr N -> reg 2093+N. Confirmed live 2026-07-09.
constexpr uint16_t REG_COOLING_SETPOINT     = 2093;  // wire addr 0x0000; cooling setpoint (whole °C, confirmed)
// wire addr 0x0001. Byte1 of the telemetry window. Observed = 40. Hypothesised
// to be a backup/aux-heater (space-heating) setpoint; this unit lacks that
// stage, so the field is UNVERIFIED — do not rely on it.
constexpr uint16_t REG_AUX_HEAT_SETPOINT    = 2094;  // wire addr 0x0001; aux/backup-heater setpoint (UNVERIFIED)
// wire addr 0x0002. Live hot-water setpoint. Confirmed 2026-07-09: dialing the
// hot-water setpoint to 38 °C moved reg2095 0x32(50) -> 0x26(38) and emitted an
// fc=0x06 write to wire addr 0x0002. This is the live target, distinct from the
// reg2012 (Cn13) ceiling.
constexpr uint16_t REG_HOT_WATER_SETPOINT   = 2095;  // wire addr 0x0002; live hot-water setpoint (confirmed)
constexpr uint16_t REG_AC_VOLTAGE           = 2101;  // A13 AC input voltage (x10 = V)
constexpr uint16_t REG_MAIN_EEV             = 2104;  // A5  main elec. expansion valve
constexpr uint16_t REG_IPM_TEMP             = 2113;  // A8  IPM module temp
constexpr uint16_t REG_REALTIME_POWER       = 2114;  // A9  real-time power (x100 = W)
constexpr uint16_t REG_FAULT_SENSOR_EE      = 2125;  // fault bitfield: sensor/EE/comm E-codes
constexpr uint16_t REG_FAULT_SENSOR_COMP    = 2126;  // fault bitfield: sensor/comm/compressor (E + r01/r02)
constexpr uint16_t REG_FAULT_ELEC           = 2127;  // fault bitfield: electrical/power-stage (r-codes + P02/P11)
constexpr uint16_t REG_FAULT                = 2128;  // fault bitfield: refrigerant/protection (P-codes)
constexpr uint16_t REG_ICON_BITS2           = 2129;  // icon bitfield #2: bit1 defrost, bit4 fan
constexpr uint16_t REG_STATUS_BYTE          = 2130;  // icon bitfield #1: bit2=comp, bit3=pump
constexpr uint16_t REG_OUTLET_WATER_TEMP    = 2132;  // o3  outlet (supply) water temp
constexpr uint16_t REG_INLET_WATER_TEMP     = 2133;  // o2  inlet (return) water temp
constexpr uint16_t REG_OUTDOOR_AMBIENT_TEMP = 2134;  // o4  ambient temp
constexpr uint16_t REG_COOL_COIL_TEMP       = 2135;  // A6  cool coil temp
constexpr uint16_t REG_COIL_TEMP            = 2136;  // A2  coil temp
constexpr uint16_t REG_SUCTION_TEMP         = 2137;  // A3  suction temp
constexpr uint16_t REG_DISCHARGE_TEMP       = 2138;  // A1  discharge temp
constexpr uint16_t REG_COMPRESSOR_FREQ      = 2141;  // A14 compressor frequency

constexpr uint16_t INPUT_START = 2093;
constexpr uint16_t INPUT_COUNT = 50;     // 2093–2142 (telemetry window, no prefix)

// ---------------------------------------------------------------------------
// Register metadata
// ---------------------------------------------------------------------------

struct RegisterInfo {
    const char *name;        // Short name (e.g. "Outlet Water Temp")
    const char *unit;        // Unit string ("°C", "Hz", …) or nullptr
    float       scale;       // Multiply raw by this for display (1.0 = none)
    bool        is_signed;   // Interpret 16-bit raw as signed (two's complement)
};

/// Look up register metadata.  Returns nullptr for unknown addresses.
const RegisterInfo *register_lookup(uint16_t address);

/// Decode raw uint16 to a human-readable string (e.g. "25 °C", "ON", "Cooling").
/// buf must be at least 64 bytes.  Returns buf.
char *register_format_value(uint16_t address, uint16_t raw, char *buf, size_t buf_len);

/// Decode a status/error bitmap register into a readable description.
/// buf must be at least 256 bytes.  Returns number of chars written.
int register_format_bitmap(uint16_t address, uint16_t raw, char *buf, size_t buf_len);

/// True if `address` is one of the five Macon fault-bitfield registers.
bool register_is_fault(uint16_t address);

/// Return a short function-code name ("Read Holding", "Write Single", etc.).
const char *function_code_name(uint8_t fc);

/// Format a signed temperature value (two's complement uint16).
inline int16_t to_signed(uint16_t raw) { return static_cast<int16_t>(raw); }

}  // namespace arctic
