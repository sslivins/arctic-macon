// Native unit test for the semantic fault-identity API (MaconFaultId /
// MaconFaultSiteId / resolution). Framework-free: prints failures, non-zero exit.

#include "macon_faults.h"
#include "macon_state.h"

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

static constexpr uint16_t BASE  = 2000;
static constexpr size_t   COUNT = 143;

int main() {
    // --- code <-> id mapping ------------------------------------------------
    CHECK(macon_fault_id_from_code("P02") == MaconFaultId::HighPressureProtection);
    CHECK(macon_fault_id_from_code("E19") == MaconFaultId::InletWaterSensor);
    CHECK(macon_fault_id_from_code("E18") == MaconFaultId::OutletWaterSensor);
    CHECK(macon_fault_id_from_code("RUN") == MaconFaultId::Unknown);  // INFO, not a fault
    CHECK(macon_fault_id_from_code("nope") == MaconFaultId::Unknown);
    CHECK(macon_fault_id_from_code(nullptr) == MaconFaultId::Unknown);

    // Non-unique code: both "E28" sites share one id.
    CHECK(macon_fault_id_from_code("E28") == MaconFaultId::EepromError);

    // --- id -> representative code / label / severity -----------------------
    CHECK(std::strcmp(macon_code_for_fault_id(MaconFaultId::HighPressureProtection), "P02") == 0);
    CHECK(std::strcmp(macon_label_for_fault_id(MaconFaultId::HighPressureProtection),
                      "High pressure protection") == 0);
    CHECK(macon_severity_for_fault_id(MaconFaultId::HighPressureProtection) == FaultSeverity::CRITICAL);
    CHECK(macon_code_for_fault_id(MaconFaultId::Unknown) == nullptr);
    CHECK(macon_severity_for_fault_id(MaconFaultId::Unknown) == FaultSeverity::INFO);

    // --- resolution (moved out of the controller) ---------------------------
    CHECK(std::strstr(macon_fault_resolution(MaconFaultId::InletWaterSensor),
                      "inlet water temperature sensor") != nullptr);
    // An id without specific guidance falls back, never null.
    const char *fallback = macon_fault_resolution(MaconFaultId::FeProtection);
    CHECK(fallback != nullptr && std::strcmp(fallback, "Contact the dealer.") == 0);
    CHECK(macon_fault_resolution(MaconFaultId::Unknown) != nullptr);

    // --- site id: distinct per (reg,bit), same id for duplicate-code sites --
    MaconFaultSiteId s1 = macon_fault_site_id(2125, 0);  // E28 outdoor
    MaconFaultSiteId s2 = macon_fault_site_id(2125, 5);  // E28 indoor
    CHECK(s1 != 0 && s2 != 0 && s1 != s2);
    CHECK(macon_fault_site_id(2127, 0) == 0);            // no fault at that bit
    CHECK(macon_fault_site_id(9999, 0) == 0);

    // --- has_fault_id over raw bytes ----------------------------------------
    // reg2127 bit7 = P02.
    CHECK(macon_has_fault_id(0, 0, 0, 0x80, 0, MaconFaultId::HighPressureProtection));
    CHECK(!macon_has_fault_id(0, 0, 0, 0x00, 0, MaconFaultId::HighPressureProtection));
    // EepromError present if EITHER site is lit.
    CHECK(macon_has_fault_id(0, 0x01, 0, 0, 0, MaconFaultId::EepromError));  // 2125:0
    CHECK(macon_has_fault_id(0, 0x20, 0, 0, 0, MaconFaultId::EepromError));  // 2125:5
    CHECK(!macon_has_fault_id(0, 0, 0, 0, 0, MaconFaultId::Unknown));

    // --- decode fills id + site ---------------------------------------------
    uint16_t regs[COUNT] = {0};
    macon_set_fault_by_code(regs, BASE, COUNT, "P02", true);
    MaconFault out[16];
    size_t n = macon_decode_faults(regs[2007 - BASE], regs[2125 - BASE],
                                   regs[2126 - BASE], regs[2127 - BASE],
                                   regs[2128 - BASE], out, 16);
    CHECK(n == 1);
    CHECK(out[0].id == MaconFaultId::HighPressureProtection);
    CHECK(out[0].site == macon_fault_site_id(2127, 7));

    // --- macon_state_has_fault (decode + gate without codes) ----------------
    MaconState st;
    decode_state(BASE, regs, COUNT, &st);
    CHECK(macon_state_has_fault(st, MaconFaultId::HighPressureProtection));
    CHECK(!macon_state_has_fault(st, MaconFaultId::InletWaterSensor));

    // --- static completeness: every non-INFO row has a real id whose code
    //     round-trips; every distinct id resolves to a code -------------------
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (fb.severity == FaultSeverity::INFO) {
            CHECK(fb.id == MaconFaultId::Unknown);   // RUN indicator
            continue;
        }
        CHECK(fb.id != MaconFaultId::Unknown);
        CHECK(macon_fault_id_from_code(fb.code) == fb.id);
        CHECK(macon_code_for_fault_id(fb.id) != nullptr);
    }
    // Enum count matches the number of distinct fault ids reachable.
    CHECK(static_cast<uint16_t>(MaconFaultId::Count) == 31);

    // --- macon_fault_bit_for_site (reverse of macon_fault_site_id) ----------
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        const MaconFaultBit &fb = MACON_FAULT_BITS[i];
        if (fb.severity == FaultSeverity::INFO) continue;  // RUN has no site id
        MaconFaultSiteId site = macon_fault_site_id(fb.reg, fb.bit);
        const MaconFaultBit *back = macon_fault_bit_for_site(site);
        CHECK(back != nullptr);
        CHECK(back->reg == fb.reg && back->bit == fb.bit);
        CHECK(back->id == fb.id);
    }
    CHECK(macon_fault_bit_for_site(0) == nullptr);
    CHECK(macon_fault_bit_for_site(0xFFFF) == nullptr);

    if (g_failures) {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("all fault-identity tests passed\n");
    return 0;
}
