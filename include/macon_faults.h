#pragma once

// ---------------------------------------------------------------------------
// Macon fault decoding — the single source of truth for the Arctic (Macon)
// heat pump's fault/protection codes.
//
// The Macon mainboard exposes its active faults as FIVE 8-bit bitfield
// registers.  Each set bit corresponds to one P/E/EE/r code exactly as shown
// on the OEM LCD and the Smart Life app.  This table was reverse-engineered
// live on the real unit (2026-07-05) one bit at a time, watching both the LCD
// and the app, then cross-referenced against the official Arctic fault
// catalog.
//
//   reg 2007  (holding window)  — run-state + P15/P16/FE/FF.  bit5 (0x20) is
//                                 the hot-water RUN indicator, NOT a fault.
//   reg 2125  (telemetry)       — sensor / EEPROM / comm E-codes.
//   reg 2126  (telemetry)       — sensor / comm / compressor (E + r01/r02).
//   reg 2127  (telemetry)       — electrical / power-stage (r-codes + P02/P11).
//   reg 2128  (telemetry)       — refrigerant / protection P-codes.
//
// Pure, dependency-free (no ESP-IDF / FreeRTOS): links into the controller,
// simulator and sniffer, and compiles natively for host unit tests.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

namespace arctic {

// Severity, used to prioritise which code to surface first.
enum class FaultSeverity : uint8_t {
    INFO     = 0,   // informational, not a fault (e.g. RUN indicator)
    WARNING  = 1,
    FAULT    = 2,
    CRITICAL = 3,
};

// ---------------------------------------------------------------------------
// Semantic fault identity.
//
// Consumers must reference faults by these library-owned identifiers, NEVER by
// the OEM code string ("P02") or a raw (reg, bit) position. The library maps an
// id to its OEM code(s), label, severity and resolution internally, so the
// controller carries no protocol knowledge.
//
// A `MaconFaultId` is the SEMANTIC identity of a fault (one per distinct OEM
// code). It is intentionally NOT unique per (reg, bit): a single code such as
// "E28" or "E05" is emitted from two distinct register bits, and all its sites
// share one id. Use `MaconFaultId` for "is this condition present?", to inject a
// fault, and to look up resolution text.
//
// For history / dedup / transition events, where each physical bit must be
// tracked separately, use `MaconFaultSiteId` (below) — the stable per-bit
// identity — instead.
// ---------------------------------------------------------------------------
enum class MaconFaultId : uint16_t {
    // reg 2007
    TempDifferenceTooLarge,     // "P15"
    OutletTempTooLow,           // "P16"
    FeProtection,               // "FE"
    FfProtection,               // "FF"
    // reg 2125
    EepromError,                // "E28" (outdoor + indoor sites)
    InletWaterSensor,           // "E19"
    OutletWaterSensor,          // "E18"
    CoolCoilSensor,             // "E13"
    E03Protection,              // "E03"
    DriverCommunication,        // "E27"
    ControllerCommunication,    // "E21"
    // reg 2126
    CompressorStartFailure,     // "r02"
    IndoorOutdoorCommunication, // "E26"
    IpmFault,                   // "r01"
    DischargeSensor,            // "E01"
    SuctionSensor,              // "E09"
    CoilSensor,                 // "E05" (reg2126 + reg2128 sites)
    AmbientSensor,              // "E22"
    // reg 2127
    AcCurrentProtection,        // "P19"
    CompressorPhaseCurrent,     // "r06"
    AcVoltageProtection,        // "r10"
    DcBusVoltageProtection,     // "r11"
    IpmTemperatureProtection,   // "r05"
    HighDischargeTemp,          // "P11"
    HighPressureProtection,     // "P02"
    // reg 2128
    LowPressureProtection,      // "P06"
    CoilOverheat,               // "P27"
    AmbientOutOfRange,          // "PC"
    P10Protection,              // "P10"
    AntifreezeProtection,       // "P30"
    WaterFlowProtection,        // "P01"

    Count,                      // number of distinct fault ids
    Unknown = 0xFFFF,           // no fault / unrecognised code
};

// Stable per-bit identity of one fault site. Opaque to consumers: treat it as a
// comparable/hashable token for history and transition detection only; do NOT
// derive a register or bit from it. Distinct sites of the same code (e.g. the
// two "E28" bits) have distinct site ids but the same MaconFaultId.
using MaconFaultSiteId = uint16_t;

// One decodable bit in one of the five Macon fault-bitfield registers.
struct MaconFaultBit {
    uint16_t      reg;        // 2007, 2125, 2126, 2127 or 2128
    uint8_t       bit;        // 0..7
    const char   *code;       // code as shown on the OEM LCD / app (e.g. "P06")
    const char   *label;      // human-readable description
    FaultSeverity severity;
    MaconFaultId  id;         // semantic identity (Unknown for the RUN indicator)
};

// The five Macon fault-bitfield register addresses.
constexpr uint16_t MACON_FAULT_REGS[]   = { 2007, 2125, 2126, 2127, 2128 };
constexpr size_t   MACON_FAULT_REGS_COUNT = 5;

// Canonical bit table, grouped by register in the order above.
extern const MaconFaultBit MACON_FAULT_BITS[];
extern const size_t         MACON_FAULT_BITS_COUNT;

// A decoded, currently-active fault.
struct MaconFault {
    const char      *code;
    const char      *label;
    FaultSeverity    severity;
    uint16_t         reg;
    uint8_t          bit;
    MaconFaultId     id;      // semantic identity (for logic / resolution)
    MaconFaultSiteId site;    // stable per-bit identity (for history / dedup)
};

// Decode active faults from the five raw fault-register bytes into `out`
// (up to `max` entries).  Returns the number written.  Non-fault bits (the
// RUN indicator) are skipped.  Results are sorted by descending severity.
size_t macon_decode_faults(uint8_t reg2007, uint8_t reg2125, uint8_t reg2126,
                           uint8_t reg2127, uint8_t reg2128,
                           MaconFault *out, size_t max);

// True if any real fault bit is set across the five registers (ignores the
// RUN indicator).
bool macon_has_fault(uint8_t reg2007, uint8_t reg2125, uint8_t reg2126,
                     uint8_t reg2127, uint8_t reg2128);

// Return the sub-range of MACON_FAULT_BITS belonging to `reg`.  Sets *count
// to the number of entries and returns a pointer to the first, or nullptr if
// the register has no fault bits.
const MaconFaultBit *macon_fault_bits_for_reg(uint16_t reg, size_t *count);

// ---------------------------------------------------------------------------
// Fault ENCODE (identity lookup + register-window injection).
//
// The decode side above turns raw registers into codes.  This side is the
// inverse: it is the single source of truth for "which (reg, bit) does code X
// live at", so NO consumer (controller demo/test injection, simulator) ever
// hardcodes a bit position.  The controller's test-only fault injection and the
// device simulator both drive the SAME real register bits through here.
//
// NOTE ON IDENTITY: a P/E code is NOT a unique key -- e.g. "E28" (outdoor +
// indoor EEPROM) and "E05" (coil sensor) each appear at two distinct
// (reg, bit) sites.  The stable identity of a fault is therefore the
// (reg, bit) pair, and history/dedup must key on that, not the code string.
// The by-code helpers below operate on ALL sites sharing a code (lighting any
// one of an "E28" pair still displays "E28"), and report how many they touched.
// ---------------------------------------------------------------------------

// Exact-identity lookup: the fault bit at (reg, bit), or nullptr if none.
const MaconFaultBit *macon_fault_bit(uint16_t reg, uint8_t bit);

// Collect every fault-bit entry whose `code` matches `code` into `out` (up to
// `max`).  Returns the total number of matches (which may exceed `max`; only
// the first `max` are written).  Handles the non-unique-code case.
size_t macon_fault_bits_for_code(const char *code, const MaconFaultBit **out,
                                 size_t max);

// Set (or clear) EVERY register bit whose code matches `code`, in the register
// window `regs[0..count)` starting at wire address `base` (real Tuya layout).
// Only the low byte of each register is significant on this unit.  Registers
// outside the window are skipped.  Returns the number of bits actually written
// (0 if the code is unknown or all its sites lie outside the window), or -1 on
// bad arguments (null regs / null code).
int macon_set_fault_by_code(uint16_t *regs, uint16_t base, size_t count,
                            const char *code, bool on);

// ---------------------------------------------------------------------------
// Semantic fault identity API (id <-> code/label/severity/resolution).
//
// This is the single source of truth that lets a consumer reference faults
// WITHOUT knowing OEM codes or (reg, bit) positions. Resolution text lives here
// too (moved out of the controller), keyed by id so multi-site codes cannot
// drift.
// ---------------------------------------------------------------------------

// Map an OEM code string to its semantic id. Returns MaconFaultId::Unknown if
// the code is not a known fault (including the "RUN" indicator).
MaconFaultId macon_fault_id_from_code(const char *code);

// Representative OEM code / label / severity for an id (nullptr / INFO if
// Unknown). For multi-site codes these describe the shared code.
const char   *macon_code_for_fault_id(MaconFaultId id);
const char   *macon_label_for_fault_id(MaconFaultId id);
FaultSeverity macon_severity_for_fault_id(MaconFaultId id);

// Human-readable remediation text for a fault id. Never null: returns a generic
// "contact the dealer" fallback for ids without specific guidance.
const char   *macon_fault_resolution(MaconFaultId id);

// Stable per-bit site identity for a (reg, bit). Opaque token; 0 if unknown.
MaconFaultSiteId macon_fault_site_id(uint16_t reg, uint8_t bit);

// True if the fault identified by `id` is active in the five raw fault bytes
// (any of its sites lit). INFO / Unknown are never "active".
bool macon_has_fault_id(uint8_t reg2007, uint8_t reg2125, uint8_t reg2126,
                        uint8_t reg2127, uint8_t reg2128, MaconFaultId id);

}  // namespace arctic
