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

// One decodable bit in one of the five Macon fault-bitfield registers.
struct MaconFaultBit {
    uint16_t      reg;        // 2007, 2125, 2126, 2127 or 2128
    uint8_t       bit;        // 0..7
    const char   *code;       // code as shown on the OEM LCD / app (e.g. "P06")
    const char   *label;      // human-readable description
    FaultSeverity severity;
};

// The five Macon fault-bitfield register addresses.
constexpr uint16_t MACON_FAULT_REGS[]   = { 2007, 2125, 2126, 2127, 2128 };
constexpr size_t   MACON_FAULT_REGS_COUNT = 5;

// Canonical bit table, grouped by register in the order above.
extern const MaconFaultBit MACON_FAULT_BITS[];
extern const size_t         MACON_FAULT_BITS_COUNT;

// A decoded, currently-active fault.
struct MaconFault {
    const char   *code;
    const char   *label;
    FaultSeverity severity;
    uint16_t      reg;
    uint8_t       bit;
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

}  // namespace arctic
