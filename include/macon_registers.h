#pragma once

#include <cstdint>
#include <cstddef>

#include "macon_faults.h"

namespace arctic {

// ---------------------------------------------------------------------------
// Register address constants
//
// A register address is the BYTE OFFSET into the unified Macon register page,
// exactly the value carried in the wire frame's `addr` field ("@N" in logs and
// docs). A read window starting at wire addr A returns @A, @A+1, ...; a
// single-register fc=0x06 write to @N carries addr N.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MACON layout (reverse-engineered from live captures on the real heat pump).
// Only registers confirmed against synchronized on-unit menu (o/A code) reads
// are named; everything else decodes as a raw byte ("Unknown").
//
// Each wire register is ONE signed/unsigned byte (see decode_byte()).
// ---------------------------------------------------------------------------

// "Telemetry" window (wire addr=0, count=50 -> @0..@49). @0 is the cooling
// setpoint (the former "7-byte prefix" is NOT static, byte0 is the setpoint).
// The controller SETS a setpoint by writing one byte to its offset (fc=0x06).
// Confirmed live 2026-07-09.
constexpr uint16_t TELEMETRY_FIRST = 0;
constexpr uint16_t TELEMETRY_COUNT = 50;   // @0..@49

constexpr uint16_t REG_COOLING_SETPOINT     = 0;   // cooling setpoint (whole °C, confirmed)
// Heating setpoint used by working modes 1 and 2. Confirmed on the bench
// 2026-09-26: with mode 1 selected the OEM wired controller shows this value,
// and dialling it to 47 emitted fc=0x06 wire addr 0x0001 data 47. (Modes 3 and
// 4 show holding @60 / @61 instead; see macon_state.h.)
constexpr uint16_t REG_HEATING_SETPOINT     = 1;   // heating setpoint (confirmed)
// Live hot-water setpoint. Confirmed 2026-07-09: dialing the hot-water
// setpoint to 38 °C moved @2 0x32(50) -> 0x26(38) and emitted an fc=0x06 write
// to wire addr 0x0002. This is the live target, distinct from the @62 (AP13)
// ceiling.
constexpr uint16_t REG_HOT_WATER_SETPOINT   = 2;   // live hot-water setpoint (confirmed)
// Confirmed live 2026-08-09 (the OEM writes value 6) and mapped on the bench
// 2026-09-26: 0=cooling, 1=heating, 2/3/4=heating variants (read-only), 5=hot
// water, 6=hot water / cooling. See MaconWorkingMode.
constexpr uint16_t REG_WORKING_MODE         = 3;
constexpr uint16_t REG_IPM_TEMP             = 20;  // A8  IPM module temp
constexpr uint16_t REG_REALTIME_POWER       = 21;  // A9  real-time power (x100 = W)
constexpr uint16_t REG_FAULT_SENSOR_EE      = 32;  // fault bitfield: sensor/EE/comm E-codes
constexpr uint16_t REG_FAULT_SENSOR_COMP    = 33;  // fault bitfield: sensor/comm/compressor (E + r01/r02)
constexpr uint16_t REG_FAULT_ELEC           = 34;  // fault bitfield: electrical/power-stage (r-codes + P02/P11)
constexpr uint16_t REG_FAULT                = 35;  // fault bitfield: refrigerant/protection (P-codes)
constexpr uint16_t REG_ICON_BITS2           = 36;  // icon bitfield #2: bit1 defrost, bit4 fan
constexpr uint16_t REG_STATUS_BYTE          = 37;  // icon bitfield #1: bit2=comp, bit3=pump
constexpr uint16_t REG_OUTLET_WATER_TEMP    = 39;  // o3  outlet (supply) water temp
constexpr uint16_t REG_INLET_WATER_TEMP     = 40;  // o2  inlet (return) water temp
constexpr uint16_t REG_OUTDOOR_AMBIENT_TEMP = 41;  // o4  ambient temp
constexpr uint16_t REG_COOL_COIL_TEMP       = 42;  // A6  cool coil temp
constexpr uint16_t REG_COIL_TEMP            = 43;  // A2  coil temp
constexpr uint16_t REG_SUCTION_TEMP         = 44;  // A3  suction temp
constexpr uint16_t REG_DISCHARGE_TEMP       = 45;  // A1  discharge temp
// A5 / A13, bench-verified 2026-11-03 (the earlier @11 / @8 mapping was wrong:
// both are constant settings bytes).
constexpr uint16_t REG_MAIN_EEV             = 47;  // A5  main EEV opening (raw, as the OEM shows it)
constexpr uint16_t REG_COMPRESSOR_FREQ      = 48;  // A14 compressor frequency
constexpr uint16_t REG_AC_VOLTAGE           = 49;  // A13 AC input voltage (V x1; the OEM shows V/10)

// "Holding" window (wire addr=50, count=58 -> @50..@107). Despite the legacy
// "holding" name, on the Macon unit this block carries telemetry, not control
// regs.
constexpr uint16_t HOLDING_FIRST = 50;
constexpr uint16_t HOLDING_COUNT = 58;     // @50..@107

constexpr uint16_t REG_AC_CURRENT           = 50;  // A4  AC input current
constexpr uint16_t REG_DC_BUS_VOLTAGE       = 51;  // A7  DC bus voltage (x10 = V)
constexpr uint16_t REG_DC_MOTOR_SPEED       = 53;  // A10 DC (fan) motor speed
constexpr uint16_t REG_FAULT_RUNSTATE       = 57;  // fault bitfield + run indicator
constexpr uint16_t REG_WATER_TANK_TEMP      = 58;  // o1  water tank temp
// AP13 "Highest setting temperature of hot water, heating mode" (20~55 °C,
// default 50). This is the CEILING/limit the mainboard enforces, NOT the live
// user setpoint (that is REG_HOT_WATER_SETPOINT @2). Confirmed: dialing the
// display setpoint down did not move @62; it stayed at its 50 °C ceiling.
constexpr uint16_t REG_HOT_WATER_CEILING    = 62;  // AP13 max hot-water temp (ceiling, not live setpoint)
// A11 / A12. Raw bytes as the OEM shows them (bench-verified 2026-11-03); the
// unit and scale are unknown. Units without pressure transducers read a
// constant -6 / 3.
constexpr uint16_t REG_HIGH_PRESSURE        = 63;  // A11 high pressure (signed byte)
constexpr uint16_t REG_LOW_PRESSURE         = 64;  // A12 low pressure (raw)
constexpr uint16_t REG_AUX_EEV              = 98;  // o6  auxiliary (EVI) EEV opening (raw)

// Operating direction, set by the reversing valve (NOT a user-selectable menu
// mode). Runtime telemetry, not static config: observed flipping 0->4 live when
// the unit switched to cooling. Validated across the full 4749-row historical
// dataset — only 0x00 (heating) and 0x04 (cooling) ever appear. See macon_state.h
// for the MaconMode decode (bit-masked on 0x04, Unknown for any unobserved bit).
constexpr uint16_t REG_OPERATING_MODE       = 99;

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
