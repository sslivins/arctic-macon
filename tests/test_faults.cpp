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

// Full register image: base @0, 143 regs (@0..@142) covers the five fault
// registers @57, @32, @33, @34, @35.
static constexpr uint16_t BASE  = 0;
static constexpr size_t   COUNT = 143;

int main() {
    // --- identity lookup: (reg, bit) is the stable key -----------------------
    const MaconFaultBit *p02 = macon_fault_bit(34, 7);
    CHECK(p02 != nullptr);
    CHECK(p02 && std::strcmp(p02->code, "P02") == 0);
    CHECK(p02 && p02->severity == FaultSeverity::CRITICAL);
    CHECK(macon_fault_bit(33, 3) == nullptr);   // no fault at that bit
    CHECK(macon_fault_bit(9999, 0) == nullptr);   // unknown register

    // The RUN indicator is an INFO bit, present but never a fault.
    const MaconFaultBit *run = macon_fault_bit(57, 5);
    CHECK(run != nullptr && run->severity == FaultSeverity::INFO);

    // --- by-code lookup: codes are NOT unique -------------------------------
    const MaconFaultBit *hits[8] = {};
    CHECK(macon_fault_bits_for_code("P02", hits, 8) == 1);   // unique
    CHECK(macon_fault_bits_for_code("E28", hits, 8) == 2);   // outdoor + indoor EE
    CHECK(macon_fault_bits_for_code("E05", hits, 8) == 2);   // @33:6 and @35:5
    CHECK(macon_fault_bits_for_code("nope", hits, 8) == 0);
    CHECK(macon_fault_bits_for_code(nullptr, hits, 8) == 0);

    // --- encode round-trips through decode ----------------------------------
    uint16_t regs[COUNT] = {0};

    // Unique code: set P02 -> exactly @34 bit7 lights, decode reports P02.
    CHECK(macon_set_fault_by_code(regs, BASE, COUNT, "P02", true) == 1);
    CHECK(regs[34 - BASE] == 0x80);
    CHECK(macon_has_fault(regs[57 - BASE], regs[32 - BASE], regs[33 - BASE],
                          regs[34 - BASE], regs[35 - BASE]));
    MaconFault out[16];
    size_t n = macon_decode_faults(regs[57 - BASE], regs[32 - BASE],
                                   regs[33 - BASE], regs[34 - BASE],
                                   regs[35 - BASE], out, 16);
    CHECK(n == 1 && std::strcmp(out[0].code, "P02") == 0);

    // Clearing it removes the bit and the fault.
    CHECK(macon_set_fault_by_code(regs, BASE, COUNT, "P02", false) == 1);
    CHECK(regs[34 - BASE] == 0x00);
    CHECK(!macon_has_fault(0, 0, 0, regs[34 - BASE], 0));

    // Non-unique code: E28 lights BOTH sites (@32:0 and @32:5) => 0x21.
    CHECK(macon_set_fault_by_code(regs, BASE, COUNT, "E28", true) == 2);
    CHECK(regs[32 - BASE] == 0x21);
    n = macon_decode_faults(0, regs[32 - BASE], 0, 0, 0, out, 16);
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
