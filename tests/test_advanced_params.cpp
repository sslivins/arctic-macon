// Native unit test for the Macon advanced ("AP") parameter guardrail.
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
    // --- register <-> AP mapping (change-and-capture verified only) --------
    // Verified regs are looked up from the table (NO 2000+ap formula).
    CHECK(advanced_register_address(20) == 2019);
    CHECK(advanced_register_address(13) == 2012);
    CHECK(advanced_register_address(48) == 2060);
    CHECK(advanced_param_reg_known(20));
    CHECK(advanced_param_reg_known(13));
    // AP24-47 tuning block is now write-verified (irregular/permuted mapping).
    CHECK(advanced_register_address(24) == 2023);
    CHECK(advanced_register_address(34) == 2035);   // swapped with AP35
    CHECK(advanced_register_address(35) == 2034);   // swapped with AP34
    CHECK(advanced_register_address(31) == 2029);   // permuted
    CHECK(advanced_register_address(40) == 2040);
    CHECK(advanced_register_address(38) == 2038);
    CHECK(advanced_param_reg_known(40));
    CHECK(advanced_param_reg_known(38));
    // AP36/37 are doc-omitted and absent from the table entirely.
    CHECK(advanced_param_lookup(36) == nullptr);
    CHECK(advanced_param_lookup(37) == nullptr);
    // Reverse lookup finds params by their confirmed register.
    CHECK(advanced_param_for_register(2019) != nullptr);
    CHECK(advanced_param_for_register(2019)->ap == 20);
    CHECK(advanced_param_for_register(2012)->ap == 13);
    CHECK(register_is_advanced_param(2019));
    CHECK(advanced_param_for_register(2035)->ap == 34);   // permuted reverse map
    CHECK(advanced_param_for_register(2034)->ap == 35);
    // Non-param registers are not advanced params.
    CHECK(advanced_param_for_register(2000) == nullptr);
    CHECK(!register_is_advanced_param(2000));
    CHECK(advanced_param_for_register(ADV_REG_UNKNOWN) == nullptr);  // 0 sentinel

    // --- verified + writable params: writes reach OK -----------------------
    // AP13 max hot-water setpoint (reg2012), range 20..55.
    CHECK(validate_advanced_write(13, 50) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(13, 20) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(13, 56) == AdvWriteResult::OUT_OF_RANGE);
    CHECK(validate_advanced_write(13, 19) == AdvWriteResult::OUT_OF_RANGE);
    // AP24-47 tuning block is write-verified AND unlocked: in-range writes OK.
    CHECK(validate_advanced_write(24, 28) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(24, 45) == AdvWriteResult::OK);   // OEM max 45
    CHECK(validate_advanced_write(24, 46) == AdvWriteResult::OUT_OF_RANGE);
    CHECK(validate_advanced_write(24, 5)  == AdvWriteResult::OUT_OF_RANGE);  // <min 10
    CHECK(validate_advanced_write(40, -1) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(40, 46) == AdvWriteResult::OUT_OF_RANGE);
    CHECK(validate_advanced_write(38, -30) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(38, 5) == AdvWriteResult::OUT_OF_RANGE);

    // --- read-only param (AP41): reg known but writes refused --------------
    CHECK(advanced_param_reg_known(41));
    CHECK(advanced_register_address(41) == 2041);
    CHECK(advanced_param_lookup(41)->read_only);
    CHECK(validate_advanced_write(41, 12) == AdvWriteResult::READ_ONLY);
    CHECK(advanced_prepare_write(41, 12, nullptr) == AdvWriteResult::READ_ONLY);
    // Value sanity still reported before the read-only gate.
    CHECK(validate_advanced_write(41, 99) == AdvWriteResult::OUT_OF_RANGE);

    // --- trigger param (AP47): momentary command, writable -----------------
    CHECK(advanced_param_reg_known(47));
    CHECK(advanced_register_address(47) == 2047);
    CHECK(advanced_param_lookup(47)->is_trigger);
    CHECK(!advanced_param_lookup(47)->read_only);
    CHECK(validate_advanced_write(47, 1) == AdvWriteResult::OK);
    {
        AdvWritePlan tplan{0xFFFF, 0xFFFF};
        CHECK(advanced_prepare_write(47, 1, &tplan) == AdvWriteResult::OK);
        CHECK(tplan.reg == 2047);
        CHECK(tplan.raw == 1);
    }
    // Ordinary config params carry neither flag.
    CHECK(!advanced_param_lookup(24)->read_only);
    CHECK(!advanced_param_lookup(24)->is_trigger);

    // --- manual block: register known but write-locked (safety) ------------
    CHECK(advanced_param_reg_known(48));
    CHECK(advanced_register_address(48) == 2060);
    CHECK(validate_advanced_write(48, 1) == AdvWriteResult::NEEDS_SIM_CONFIRM);
    CHECK(validate_advanced_write(49, 30) == AdvWriteResult::NEEDS_SIM_CONFIRM);
    // Out-of-range still reported before the lock.
    CHECK(validate_advanced_write(48, 2) == AdvWriteResult::OUT_OF_RANGE);

    // --- every param is localizable: all rows carry stable name/detail keys --
    // Phase 3 extended the msg_id scheme from the K-ratio block to the entire
    // table, so no param name/detail is left un-keyed for translation.
    CHECK(advanced_param_count() > 0);
    for (size_t i = 0; i < advanced_param_count(); ++i) {
        const AdvancedParam *p = advanced_param_at(i);
        CHECK(p != nullptr);
        CHECK(p->name_msg_id != nullptr && p->name_msg_id[0] != '\0');
        CHECK(p->detail_msg_id != nullptr && p->detail_msg_id[0] != '\0');
    }
    // Spot-check a couple of the newly-keyed non-K-ratio params.
    CHECK(std::strcmp(advanced_param_lookup(13)->name_msg_id, "ap.max_hw_setpoint.name") == 0);
    CHECK(std::strcmp(advanced_param_lookup(47)->detail_msg_id, "ap.water_system_cleaning.detail") == 0);

    // --- enum (K-ratio) params: verified + writable ------------------------
    // AP14 valid readings {0,1,2,4,8,12,16,20}, reg2111 confirmed.
    CHECK(validate_advanced_write(14, 8) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(14, 3) == AdvWriteResult::NOT_IN_ENUM);
    CHECK(validate_advanced_write(14, 20) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(44, 0) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(44, 2) == AdvWriteResult::OK);
    CHECK(validate_advanced_write(44, 3) == AdvWriteResult::NOT_IN_ENUM);

    // --- enum display options (locale-independent presentation metadata) ----
    // Every K-ratio param (AP14..AP20) shares the same 8 options, aligned 1:1
    // with its enum_vals; the wire code is the ONLY thing that hits the bus.
    for (uint8_t ap = 14; ap <= 20; ++ap) {
        const AdvancedParam *p = advanced_param_lookup(ap);
        CHECK(p != nullptr);
        // Every K-ratio param carries stable i18n keys for its localizable
        // name/detail; the library owns identity, UIs own translation and fall
        // back to the English `name`/`detail` when a key is untranslated.
        CHECK(p->name_msg_id != nullptr && p->name_msg_id[0] != '\0');
        CHECK(p->detail_msg_id != nullptr && p->detail_msg_id[0] != '\0');
        CHECK(advanced_enum_option_count(ap) == p->enum_count);
        for (uint8_t i = 0; i < p->enum_count; ++i) {
            const AdvEnumOption *opt = advanced_enum_option_at(ap, i);
            CHECK(opt != nullptr);
            // Options must match the validation enum, in the same order.
            CHECK(opt->wire == static_cast<int16_t>(p->enum_vals[i]));
            CHECK(opt->label != nullptr && opt->label[0] != '\0');
            CHECK(opt->msg_id != nullptr && opt->msg_id[0] != '\0');
            CHECK(opt->en_default != nullptr && opt->en_default[0] != '\0');
        }
    }
    // Reverse lookup by wire code returns the human meaning; the library owns
    // "12 == 6-step per 1 Hz" so no UI ever hardcodes it.
    const AdvEnumOption *o12 = advanced_enum_option_for_wire(18, 12);
    CHECK(o12 != nullptr);
    CHECK(std::strcmp(o12->label, "3") == 0);
    CHECK(std::strcmp(o12->msg_id, "kratio_reduce") == 0);
    CHECK(o12->arg_a == 6 && o12->arg_b == 1);
    const AdvEnumOption *o0 = advanced_enum_option_for_wire(18, 0);
    CHECK(o0 != nullptr && std::strcmp(o0->msg_id, "kratio_none") == 0);
    CHECK(std::strcmp(o0->label, "0") == 0);
    const AdvEnumOption *pump_follow = advanced_enum_option_for_wire(44, 1);
    CHECK(pump_follow != nullptr);
    CHECK(std::strcmp(pump_follow->label, "Follow compressor") == 0);
    // AP18 is K5: its localizable name/detail keys follow the stable scheme.
    CHECK(std::strcmp(advanced_param_lookup(18)->name_msg_id, "ap.freq_ratio_k5.name") == 0);
    CHECK(std::strcmp(advanced_param_lookup(18)->detail_msg_id, "ap.freq_ratio_k5.detail") == 0);
    // Temperature thresholds in K-ratio details are unit-agnostic {T:<c>} tokens
    // (canonical Celsius); the UI converts+labels them. The heating/hot-water
    // params (K1..K6) carry a threshold window; K7 (cooling) carries none.
    for (uint8_t ap = 14; ap <= 19; ++ap) {
        CHECK(std::strstr(advanced_param_lookup(ap)->detail, "{T:") != nullptr);
    }
    CHECK(std::strstr(advanced_param_lookup(20)->detail, "{T:") == nullptr);
    CHECK(std::strstr(advanced_param_lookup(18)->detail, "{T:-9}") != nullptr);
    CHECK(std::strstr(advanced_param_lookup(18)->detail, "{T:18}") != nullptr);
    CHECK(std::strstr(advanced_param_lookup(18)->detail, "{T:43}") != nullptr);
    // A wire code that is not a valid reading has no option.
    CHECK(advanced_enum_option_for_wire(18, 3) == nullptr);
    // Non-enum params expose no options.
    CHECK(advanced_enum_option_count(13) == 0);
    CHECK(advanced_enum_option_at(13, 0) == nullptr);
    CHECK(advanced_enum_option_for_wire(13, 0) == nullptr);

    // --- write-plan builder ------------------------------------------------
    {
        AdvWritePlan plan{0xFFFF, 0xFFFF};
        CHECK(advanced_prepare_write(20, 8, &plan) == AdvWriteResult::OK);
        CHECK(plan.reg == 2019);
        CHECK(plan.raw == 8);
        // A verified negative param encodes as 8-bit two's-complement in the
        // low byte (high byte 0): -5 -> 0x00FB, NOT 0xFFFB.
        CHECK(advanced_prepare_write(40, -5, &plan) == AdvWriteResult::OK);
        CHECK(plan.reg == 2040);
        CHECK(plan.raw == 0x00FB);
        CHECK((plan.raw & 0xFF00) == 0);
        // Round-trip: decoding the wire value recovers the engineering value.
        CHECK(advanced_decode_raw(40, plan.raw) == -5);
        CHECK(advanced_decode_raw(40, 0x00FF) == -1);   // reg2040 live: 255 -> -1
        CHECK(advanced_decode_raw(38, 0x00E2) == -30);  // reg2038 live: 226 -> -30
        // Unsigned params are not sign-extended.
        CHECK(advanced_decode_raw(20, 8) == 8);
        // A refused write must NOT populate the plan (leave a plan the caller
        // can't accidentally send). Locked / read-only / OOB all return non-OK.
        CHECK(advanced_prepare_write(48, 1, nullptr) == AdvWriteResult::NEEDS_SIM_CONFIRM);
        CHECK(advanced_prepare_write(41, 12, nullptr) == AdvWriteResult::READ_ONLY);
        CHECK(advanced_prepare_write(13, 100, nullptr) == AdvWriteResult::OUT_OF_RANGE);
    }

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
        CHECK(p->ap >= ADV_AP_MIN && p->ap <= ADV_AP_MAX);
        CHECK(p->min_val <= p->max_val);
        CHECK(p->default_val >= p->min_val && p->default_val <= p->max_val);
        // Every param carries a clean name and a non-empty detail explanation.
        CHECK(p->name != nullptr && p->name[0] != '\0');
        CHECK(p->detail != nullptr && p->detail[0] != '\0');
    }

    // --- categories --------------------------------------------------------
    CHECK(advanced_category_count() == 7);
    CHECK(advanced_category_at(advanced_category_count()) == nullptr);
    // Every param's category is one of the canonical category strings.
    for (size_t i = 0; i < advanced_param_count(); ++i) {
        const AdvancedParam *p = advanced_param_at(i);
        CHECK(p->category != nullptr);
        bool known = false;
        for (size_t c = 0; c < advanced_category_count(); ++c) {
            if (std::strcmp(p->category, advanced_category_at(c)) == 0) { known = true; break; }
        }
        CHECK(known);
    }
    // Every canonical category is used by at least one param (no empty groups).
    for (size_t c = 0; c < advanced_category_count(); ++c) {
        bool used = false;
        for (size_t i = 0; i < advanced_param_count(); ++i) {
            if (std::strcmp(advanced_param_at(i)->category, advanced_category_at(c)) == 0) { used = true; break; }
        }
        CHECK(used);
    }
    // Spot-check a few assignments.
    CHECK(std::strcmp(advanced_param_lookup(14)->category, "Frequency") == 0);
    CHECK(std::strcmp(advanced_param_lookup(30)->category, "Defrost") == 0);
    CHECK(std::strcmp(advanced_param_lookup(41)->category, "EEV") == 0);
    CHECK(std::strcmp(advanced_param_lookup(48)->category, "Manual/Test") == 0);

    // --- result names ------------------------------------------------------
    CHECK(std::strcmp(adv_write_result_name(AdvWriteResult::OK), "OK") == 0);

    if (g_failures == 0) {
        std::printf("all advanced-param tests passed\n");
        return 0;
    }
    std::printf("%d advanced-param test(s) FAILED\n", g_failures);
    return 1;
}
