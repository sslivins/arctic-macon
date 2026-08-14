// Native unit test for Macon fault decode + encode (identity/code lookup and
// register-window injection). Framework-free: prints failures, non-zero exit.

#include "macon_faults.h"

#include <cstdio>
#include <cstring>

using namespace arctic;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// Real Tuya window: base 2000, 143 regs (2000..2142) covers the five fault
// registers 2007, 2125, 2126, 2127, 2128.
static constexpr uint16_t BASE  = 2000;
static constexpr size_t   COUNT = 143;

int main() {
    // --- identity lookup: (reg, bit) is the stable key -----------------------
    const MaconFaultBit *p02 = macon_fault_bit(2127, 7);
    CHECK(p02 != nullptr);
    CHECK(p02 && std::strcmp(p02->code, "P02") == 0);
    CHECK(p02 && p02->severity == FaultSeverity::CRITICAL);
    CHECK(macon_fault_bit(2127, 0) == nullptr);   // no fault at that bit
    CHECK(macon_fault_bit(9999, 0) == nullptr);   // unknown register

    // The RUN indicator is an INFO bit, present but never a fault.
    const MaconFaultBit *run = macon_fault_bit(2007, 5);
    CHECK(run != nullptr && run->severity == FaultSeverity::INFO);

    // --- by-code lookup: codes are NOT unique -------------------------------
    const MaconFaultBit *hits[8] = {};
    CHECK(macon_fault_bits_for_code("P02", hits, 8) == 1);   // unique
    CHECK(macon_fault_bits_for_code("E28", hits, 8) == 2);   // outdoor + indoor EE
    CHECK(macon_fault_bits_for_code("E05", hits, 8) == 2);   // 2126:6 and 2128:5
    CHECK(macon_fault_bits_for_code("nope", hits, 8) == 0);
    CHECK(macon_fault_bits_for_code(nullptr, hits, 8) == 0);

    // --- encode round-trips through decode ----------------------------------
    uint16_t regs[COUNT] = {0};

    // Unique code: set P02 -> exactly reg2127 bit7 lights, decode reports P02.
    CHECK(macon_set_fault_by_code(regs, BASE, COUNT, "P02", true) == 1);
    CHECK(regs[2127 - BASE] == 0x80);
    CHECK(macon_has_fault(regs[2007 - BASE], regs[2125 - BASE], regs[2126 - BASE],
                          regs[2127 - BASE], regs[2128 - BASE]));
    MaconFault out[16];
    size_t n = macon_decode_faults(regs[2007 - BASE], regs[2125 - BASE],
                                   regs[2126 - BASE], regs[2127 - BASE],
                                   regs[2128 - BASE], out, 16);
    CHECK(n == 1 && std::strcmp(out[0].code, "P02") == 0);

    // Clearing it removes the bit and the fault.
    CHECK(macon_set_fault_by_code(regs, BASE, COUNT, "P02", false) == 1);
    CHECK(regs[2127 - BASE] == 0x00);
    CHECK(!macon_has_fault(0, 0, 0, regs[2127 - BASE], 0));

    // Non-unique code: E28 lights BOTH sites (2125:0 and 2125:5) => 0x21.
    CHECK(macon_set_fault_by_code(regs, BASE, COUNT, "E28", true) == 2);
    CHECK(regs[2125 - BASE] == 0x21);
    n = macon_decode_faults(0, regs[2125 - BASE], 0, 0, 0, out, 16);
    // Both decode as "E28" (distinct (reg,bit) identities, same display code).
    CHECK(n == 2);
    CHECK(std::strcmp(out[0].code, "E28") == 0 && std::strcmp(out[1].code, "E28") == 0);

    // --- guards -------------------------------------------------------------
    CHECK(macon_set_fault_by_code(nullptr, BASE, COUNT, "P02", true) == -1);
    CHECK(macon_set_fault_by_code(regs, BASE, COUNT, nullptr, true) == -1);
    // Code whose register lies outside the window writes nothing.
    uint16_t tiny[1] = {0};
    CHECK(macon_set_fault_by_code(tiny, BASE, 1, "P02", true) == 0);

    if (g_failures) {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("all fault tests passed\n");
    return 0;
}
