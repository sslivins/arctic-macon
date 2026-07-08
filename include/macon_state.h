#pragma once

// ---------------------------------------------------------------------------
// Macon decoded-state model.
//
// This is the domain layer of the library: it owns the knowledge of WHICH wire
// register carries WHICH field and HOW to interpret it, and turns a raw Macon
// register image into a single, authoritative decoded struct (MaconState).
//
// Consumers (arctic-controller / -sniffer / -simulator) must NOT hardcode
// register addresses or re-implement this mapping. They own only hardware I/O,
// tasking/locking, and adapting MaconState into their own presentation types
// (e.g. the controller's legacy WorkingMode enum + status/error bitfields).
//
// Native semantics only: MaconMode uses raw-wire-aligned meaning (Heating vs
// Cooling), NOT any consumer's legacy enum values. Casting a MaconMode onto a
// legacy enum is a bug; consumers must translate explicitly.
//
// Pure, dependency-free (no ESP-IDF / FreeRTOS): host-unit-testable with plain
// register arrays.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

namespace arctic {

// ---------------------------------------------------------------------------
// Operating mode (reg2049, reversing-valve direction)
// ---------------------------------------------------------------------------

enum class MaconMode : uint8_t {
    Heating = 0,      // reversing valve de-energized
    Cooling = 4,      // reversing valve energized
    Unknown = 0xFF,   // any raw pattern we have not ground-truthed
};

// reg2049 behaves as a bitfield. Only bit2 (0x04) has ever been observed
// toggling (heating<->cooling) across the full historical dataset; no other bit
// is ever set. Decode on that bit specifically so an unrelated future flag bit
// can't break cooling detection, but still surface Unknown if an unobserved bit
// appears rather than silently guessing.
constexpr uint8_t MODE_COOL_BIT   = 0x04;   // reversing valve energized = cooling
constexpr uint8_t MODE_KNOWN_MASK = 0x04;   // the only bit whose meaning is confirmed

/// Decode a raw reg2049 value into a MaconMode. `raw` is the wire register value
/// (one byte on this unit). Returns Unknown if any unconfirmed bit is set.
MaconMode decode_mode(uint16_t raw);

/// Human-readable name for a MaconMode ("Heating", "Cooling", "Unknown").
const char *mode_name(MaconMode m);

// ---------------------------------------------------------------------------
// Decoded state
// ---------------------------------------------------------------------------

// A field's *_valid flag is true when the source register was present in the
// decoded window. (With the current bare-array input every in-range register is
// "present"; precise per-register presence tracking arrives with the Phase 2
// MaconRegisterImage. The flags exist now so that migration is not a
// struct-breaking change.)
struct MaconState {
    // Operating mode (reversing-valve direction).
    MaconMode mode;
    bool      mode_valid;

    // Run / flags. `running` follows the mainboard run-state (reg2007 == 0x20);
    // compressor/pump/defrost/fan come from the reg2129/reg2130 icon bitfields.
    bool     running;
    bool     compressor_on;
    bool     pump_on;
    bool     defrost_on;
    bool     fan_on;
    uint16_t fan_level;         // reg2003 A10 DC motor speed (raw level)

    // Temperatures (signed whole °C).
    int16_t water_tank_c;       bool water_tank_valid;      // reg2008 o1
    int16_t outlet_c;           bool outlet_valid;          // reg2132 o3 (supply)
    int16_t inlet_c;            bool inlet_valid;           // reg2133 o2 (return)
    int16_t outdoor_ambient_c;  bool outdoor_ambient_valid; // reg2134 o4
    int16_t indoor_coil_c;      bool indoor_coil_valid;     // reg2135 A6 cool coil
    int16_t ipm_c;              bool ipm_valid;             // reg2113 A8
    int16_t discharge_c;        bool discharge_valid;       // reg2138 A1
    int16_t suction_c;          bool suction_valid;         // reg2137 A3
    int16_t outdoor_coil_c;     bool outdoor_coil_valid;    // reg2136 A2

    // Setpoint.
    int16_t hot_water_setpoint; bool hot_water_setpoint_valid;  // reg2012

    // Electrical.
    uint16_t ac_current;        bool ac_current_valid;      // reg2000 A4
    uint16_t ac_voltage;        bool ac_voltage_valid;      // reg2101 A13
    uint16_t dc_voltage;        bool dc_voltage_valid;      // reg2001 A7 (already x10 => volts)
    uint16_t primary_eev;       bool primary_eev_valid;     // reg2140 A5
    uint16_t compressor_freq;   bool compressor_freq_valid; // reg2141 A14

    // Raw Macon fault/run registers. These are address-encapsulated here so the
    // consumer no longer needs the reg numbers; the bit->fault decode itself is
    // still applied by the consumer's legacy pipeline in Phase 1. Native fault
    // decode (typed enum) is deferred to Phase 2.
    uint8_t fault_run;    // reg2007 (bit5 0x20 = RUN indicator, not a fault)
    uint8_t fault_ee;     // reg2125 sensor/EE/comm E-codes
    uint8_t fault_comp;   // reg2126 sensor/comm/compressor
    uint8_t fault_elec;   // reg2127 electrical/power-stage
    uint8_t fault_ref;    // reg2128 refrigerant/protection
    bool    faults_valid; // all five present
};

/// Outcome summary of a decode_state() call.
struct DecodeStatus {
    uint16_t registers_present;  // in-range registers the window covered
    uint16_t registers_expected; // registers decode_state tried to read
};

/// Decode a Macon register image into MaconState.
///
/// `regs[i]` is the value of wire register (base + i); one byte per register
/// (values are stored in uint16_t but only the low byte is significant on this
/// unit). Registers outside [base, base + count) decode as not-valid and leave
/// their field at 0 / Unknown. `out` must be non-null.
///
/// Behaviour is a faithful port of the controller's former applyMaconMapping()
/// register->field logic, plus the reg2049 mode field.
DecodeStatus decode_state(uint16_t base, const uint16_t *regs, size_t count,
                          MaconState *out);

// ---------------------------------------------------------------------------
// Operating state
// ---------------------------------------------------------------------------
//
// The overall thing the unit is *doing right now*, layering the run/fault flags
// on top of the reversing-valve direction. This is the single, ground-truthed
// place for that derivation so every consumer (controller hero card, sniffer
// dashboard, simulator) presents the same state instead of each re-deriving it.
//
// Direction stays native/neutral here: heating is reported as Heating, never a
// consumer-specific label like "Floor Heating" — the consumer applies its own
// installation-specific wording.

enum class MaconOperation : uint8_t {
    Unknown,   // status registers were absent from the decoded window
    Off,       // enabled bit (reg2007 0x20) clear — unit standby
    Fault,     // an active fault is present
    Idle,      // enabled but the compressor is not running
    Defrost,   // defrost cycle active
    Cooling,   // compressor running, reversing valve = cooling
    Heating,   // compressor running, reversing valve = heating
};

/// Derive the overall operating state from a decoded MaconState.
///
/// Priority: Unknown (no data) -> Fault -> Off -> Defrost -> Idle -> direction.
/// The "compressor running" gate is the reg2141 frequency (> 0), NOT the reg2130
/// icon bit: on this unit reg2130 reads 0 even while the compressor runs, so the
/// icon bit would falsely report Idle.
MaconOperation decode_operation(const MaconState &s);

/// Human-readable name for a MaconOperation (e.g. "Heating", "Idle", "Defrost").
const char *operation_name(MaconOperation op);

}  // namespace arctic
