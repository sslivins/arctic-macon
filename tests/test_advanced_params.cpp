// Native unit test for the Macon advanced ("Cn") parameter guardrail.
// Framework-free: prints failures and returns non-zero on any failure.

#include "macon_advanced_params.h"

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

int main() {
    // --- register <-> Cn mapping -------------------------------------------
    CHECK(advanced_register_address(40) == 2040);
    CHECK(advanced_register_address(38) == 2038);
    CHECK(advanced_param_for_register(2040) != nullptr);
    CHECK(advanced_param_for_register(2040)->cn == 40);
    CHECK(register_is_advanced_param(2040));
    // 2000-2012 are telemetry, not advanced params.
    CHECK(advanced_param_for_register(2012) == nullptr);
    CHECK(advanced_param_for_register(2000) == nullptr);
    CHECK(!register_is_advanced_param(2000));

    // --- anchored (index-confirmed) params: writes reach OK ----------------
    // Cn40 cooling comp-stop ambient, range -45..45, confirmed.
    CHECK(validate_advanced_write(40, -1) == AdvWriteResult::OK);   // owner ground-truth value
    CHECK(validate_advanced_write(40, 15) == AdvWriteResult::OK);   // default
    CHECK(validate_advanced_write(40, 46) == AdvWriteResult::OUT_OF_RANGE);
    CHECK(validate_advanced_write(40, -46) == AdvWriteResult::OUT_OF_RANGE);

    // Cn38 low-ambient protection, effective range -30..0, confirmed.
    CHECK(validate_advanced_write(38, -30) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(38, 0) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(38, -31) == AdvWriteResult::OUT_OF_RANGE);  // PDF: invalid below -30
    CHECK(validate_advanced_write(38, 5) == AdvWriteResult::OUT_OF_RANGE);

    // --- non-anchored params: refused until sim-confirmed ------------------
    // Cn24 in range, but index not yet confirmed -> refuse.
    CHECK(validate_advanced_write(24, 28) == AdvWriteResult::NEEDS_SIM_CONFIRM);
    // Value sanity is reported before the sim-confirm gate.
    CHECK(validate_advanced_write(24, 5) == AdvWriteResult::OUT_OF_RANGE);

    // --- enum (K-ratio) params ---------------------------------------------
    // Cn14 valid readings {0,1,2,4,8,12,16,20}; still unconfirmed.
    CHECK(validate_advanced_write(14, 8) == AdvWriteResult::NEEDS_SIM_CONFIRM);
    CHECK(validate_advanced_write(14, 3) == AdvWriteResult::NOT_IN_ENUM);
    CHECK(validate_advanced_write(14, 20) == AdvWriteResult::NEEDS_SIM_CONFIRM);

    // --- unknown parameter -------------------------------------------------
    CHECK(validate_advanced_write(99, 0) == AdvWriteResult::UNKNOWN_PARAM);
    CHECK(advanced_param_lookup(99) == nullptr);

    // --- table iteration ---------------------------------------------------
    CHECK(advanced_param_count() > 0);
    CHECK(advanced_param_at(0) != nullptr);
    CHECK(advanced_param_at(advanced_param_count()) == nullptr);
    // Every table entry is a valid advanced-param register.
    for (size_t i = 0; i < advanced_param_count(); ++i) {
        const AdvancedParam *p = advanced_param_at(i);
        CHECK(p->cn >= ADV_CN_MIN && p->cn <= ADV_CN_MAX);
        CHECK(p->min_val <= p->max_val);
        CHECK(p->default_val >= p->min_val && p->default_val <= p->max_val);
    }

    // --- result names ------------------------------------------------------
    CHECK(std::strcmp(adv_write_result_name(AdvWriteResult::OK), "OK") == 0);

    if (g_failures == 0) {
        std::printf("all advanced-param tests passed\n");
        return 0;
    }
    std::printf("%d advanced-param test(s) FAILED\n", g_failures);
    return 1;
}
