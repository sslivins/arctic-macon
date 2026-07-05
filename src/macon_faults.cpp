#include "macon_faults.h"

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
    { 2007, 0, "P15", "Temp difference too large (PT)",     FaultSeverity::WARNING  },
    { 2007, 1, "P16", "Outlet temp too low (PT)",           FaultSeverity::WARNING  },
    { 2007, 2, "FE",  "FE protection",                      FaultSeverity::FAULT    },
    { 2007, 3, "FF",  "FF protection",                      FaultSeverity::FAULT    },
    { 2007, 5, "RUN", "Hot-water run indicator",            FaultSeverity::INFO     },

    // reg 2125 — sensor / EEPROM / comm E-codes.
    { 2125, 0, "E28", "Outdoor EEPROM error",               FaultSeverity::FAULT    },
    { 2125, 1, "E19", "Inlet water temp sensor",            FaultSeverity::FAULT    },
    { 2125, 2, "E18", "Outlet water temp sensor",           FaultSeverity::FAULT    },
    { 2125, 3, "E13", "Cool coil temp sensor",              FaultSeverity::FAULT    },
    { 2125, 4, "E03", "E03 protection",                     FaultSeverity::FAULT    },
    { 2125, 5, "E28", "Indoor EEPROM error",                FaultSeverity::FAULT    },
    { 2125, 6, "E27", "Driver communication",               FaultSeverity::CRITICAL },
    { 2125, 7, "E21", "Controller communication",           FaultSeverity::CRITICAL },

    // reg 2126 — sensor / comm / compressor (E + r01/r02).
    { 2126, 0, "r02", "Compressor start failure",           FaultSeverity::FAULT    },
    { 2126, 1, "E26", "Indoor/outdoor communication",       FaultSeverity::CRITICAL },
    { 2126, 2, "r01", "IPM fault",                          FaultSeverity::CRITICAL },
    { 2126, 4, "E01", "Discharge temp sensor",              FaultSeverity::FAULT    },
    { 2126, 5, "E09", "Suction temp sensor",                FaultSeverity::FAULT    },
    { 2126, 6, "E05", "Coil temp sensor",                   FaultSeverity::FAULT    },
    { 2126, 7, "E22", "Ambient temp sensor",                FaultSeverity::FAULT    },

    // reg 2127 — electrical / power-stage (r-codes + P02/P11).
    { 2127, 1, "P19", "AC current protection",              FaultSeverity::FAULT    },
    { 2127, 2, "r06", "Compressor phase current",           FaultSeverity::FAULT    },
    { 2127, 3, "r10", "AC voltage protection",              FaultSeverity::FAULT    },
    { 2127, 4, "r11", "DC bus voltage protection",          FaultSeverity::FAULT    },
    { 2127, 5, "r05", "IPM temperature protection",         FaultSeverity::FAULT    },
    { 2127, 6, "P11", "High discharge temp",                FaultSeverity::FAULT    },
    { 2127, 7, "P02", "High pressure protection",           FaultSeverity::CRITICAL },

    // reg 2128 — refrigerant / protection P-codes.
    { 2128, 0, "P06", "Low pressure protection",            FaultSeverity::CRITICAL },
    { 2128, 1, "P27", "Coil overheat",                      FaultSeverity::FAULT    },
    { 2128, 2, "PC",  "Ambient too high/low (PT)",          FaultSeverity::WARNING  },
    { 2128, 3, "P10", "P10 protection",                     FaultSeverity::FAULT    },
    { 2128, 4, "P30", "Antifreeze protection",              FaultSeverity::WARNING  },
    { 2128, 5, "E05", "Coil temp sensor",                   FaultSeverity::FAULT    },
    { 2128, 7, "P01", "Water flow protection",              FaultSeverity::CRITICAL },
};

const size_t MACON_FAULT_BITS_COUNT = sizeof(MACON_FAULT_BITS) / sizeof(MACON_FAULT_BITS[0]);

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
