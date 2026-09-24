// Native unit tests for the named-field / catalog / slave-side wire API
// (macon_fields.h, MaconImage slave I/O, fault site catalog).
// Framework-free: prints failures and returns non-zero on any failure.

#include "macon_fields.h"
#include "macon_faults.h"
#include "macon_image.h"
#include "tuya_codec.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace arctic;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// Decode what a master would see after polling every known window of `sim`.
static MaconState master_view(const MaconImage &sim) {
    MaconImage master;
    for (size_t w = 0; w < tuya_codec::KNOWN_WINDOWS_COUNT; ++w) {
        const tuya_codec::RegWindow &win = tuya_codec::KNOWN_WINDOWS[w];
        uint8_t buf[256];
        const size_t n = sim.read_window(win.field_a, win.field_b, buf, sizeof(buf));
        CHECK(n == win.field_b);
        master.ingest_bytes(win.reg_base, buf + win.prefix_len, n - win.prefix_len);
    }
    MaconState s;
    master.decode(&s);
    return s;
}

static MaconState local_view(const MaconImage &img) {
    MaconState s;
    img.decode(&s);
    return s;
}

static void test_field_lookup() {
    CHECK(macon_field_find("outlet_water_temp") != nullptr);
    CHECK(macon_field_find("nope") == nullptr);
    CHECK(macon_field_find(nullptr) == nullptr);
    // Aliases the controller's demo endpoint already accepts.
    const MaconFieldDesc *eev = macon_field_find("primary_eev");
    CHECK(eev != nullptr);
    CHECK(macon_field_find("main_eev") == eev);
    CHECK(macon_field_find("primary_eev_opening") == eev);
    // Names are unique across names and aliases.
    for (size_t i = 0; i < MACON_FIELDS_COUNT; ++i) {
        CHECK(macon_field_find(MACON_FIELDS[i].name) == &MACON_FIELDS[i]);
    }
}

static void test_number_round_trip() {
    for (size_t i = 0; i < MACON_FIELDS_COUNT; ++i) {
        const MaconFieldDesc &d = MACON_FIELDS[i];
        if (d.kind != MaconFieldKind::Number) continue;
        for (int32_t v : { d.min, d.max, d.step * 7, d.min < 0 ? -6 * d.step : d.step }) {
            MaconImage img;
            img.fill_baseline();
            CHECK(macon_field_set(img, d, v) == MaconSetResult::Ok);
            // Same value locally and as seen by a master over the wire.
            for (const MaconState &s : { local_view(img), master_view(img) }) {
                int32_t got = 0;
                const bool valid = macon_field_get(s, d, &got);
                if (!valid || got != v) {
                    std::printf("  field %s: set %ld got %ld valid=%d\n", d.name,
                                (long)v, (long)got, (int)valid);
                }
                CHECK(valid);
                CHECK(got == v);
            }
        }
    }
}

static void test_strict_rejects() {
    const MaconFieldDesc *power = macon_field_find("realtime_power");
    const MaconFieldDesc *temp = macon_field_find("outlet_water_temp");
    const MaconFieldDesc *fan = macon_field_find("fan_on");
    MaconImage img;
    img.fill_baseline();
    uint16_t before[MaconImage::window_count()], after[MaconImage::window_count()];
    uint16_t base = 0;
    img.raw(&base, before, MaconImage::window_count());
    CHECK(macon_field_set(img, *power, 2850) == MaconSetResult::OutOfRange);  // not a multiple of 100
    CHECK(macon_field_set(img, *power, -100) == MaconSetResult::OutOfRange);
    CHECK(macon_field_set(img, *temp, 128) == MaconSetResult::OutOfRange);
    CHECK(macon_field_set(img, *temp, -129) == MaconSetResult::OutOfRange);
    CHECK(macon_field_set(img, *fan, 2) == MaconSetResult::OutOfRange);
    img.raw(&base, after, MaconImage::window_count());
    CHECK(std::memcmp(before, after, sizeof(before)) == 0);   // untouched
    // Legacy (non-strict) path still clamps and reports it.
    CHECK(macon_field_set(img, *power, 2850, false) == MaconSetResult::Truncated);
}

static void test_flags_and_enums() {
    for (size_t i = 0; i < MACON_FIELDS_COUNT; ++i) {
        const MaconFieldDesc &d = MACON_FIELDS[i];
        if (d.kind != MaconFieldKind::Flag) continue;
        for (int32_t v : { 1, 0 }) {
            MaconImage img;
            img.fill_baseline();
            // Set every OTHER flag so a flag sharing a register can't be confused.
            for (size_t j = 0; j < MACON_FIELDS_COUNT; ++j) {
                if (j != i && MACON_FIELDS[j].kind == MaconFieldKind::Flag) {
                    macon_field_set(img, MACON_FIELDS[j], 1 - v);
                }
            }
            CHECK(macon_field_set(img, d, v) == MaconSetResult::Ok);
            int32_t got = -1;
            CHECK(macon_field_get(master_view(img), d, &got));
            if (got != v) std::printf("  flag %s: set %ld got %ld\n", d.name, (long)v, (long)got);
            CHECK(got == v);
        }
    }

    const MaconFieldDesc *wm = macon_field_find("working_mode");
    for (const char *key : { "cooling", "floor_heating", "fan_coil_heating", "hot_water", "auto" }) {
        const MaconWorkingMode m = macon_working_mode_from_key(key);
        CHECK(m != MaconWorkingMode::Unknown);
        CHECK(std::strcmp(macon_working_mode_key(m), key) == 0);
        MaconImage img;
        img.fill_baseline();
        CHECK(macon_field_set(img, *wm, static_cast<int32_t>(m)) == MaconSetResult::Ok);
        int32_t got = -1;
        CHECK(macon_field_get(master_view(img), *wm, &got));
        CHECK(got == static_cast<int32_t>(m));
    }
    CHECK(macon_working_mode_from_key("turbo") == MaconWorkingMode::Unknown);
    {
        MaconImage img;
        CHECK(macon_field_set(img, *wm, 3) == MaconSetResult::OutOfRange);   // not a mode
    }

    const MaconFieldDesc *dir = macon_field_find("operating_direction");
    for (const char *key : { "heating", "cooling" }) {
        const MaconMode m = macon_direction_from_key(key);
        MaconImage img;
        img.fill_baseline();
        img.set_working_mode(MaconWorkingMode::HotWater);
        CHECK(macon_field_set(img, *dir, static_cast<int32_t>(m)) == MaconSetResult::Ok);
        const MaconState s = master_view(img);
        CHECK(s.mode == m);
        CHECK(s.working_mode == MaconWorkingMode::HotWater);   // independent of direction
    }
}

static void test_compressor_running_is_frequency() {
    // The icon bit alone must not make the unit look like it's running.
    MaconImage img;
    img.fill_baseline();
    img.set_flag(MaconFlag::UnitOn, true);
    macon_field_set(img, *macon_field_find("compressor_icon"), 1);
    MaconState s = master_view(img);
    CHECK(s.compressor_on);
    CHECK(s.compressor_freq == 0);
    CHECK(decode_operation(s) == MaconOperation::Idle);
    macon_field_set(img, *macon_field_find("compressor_freq"), 50);
    s = master_view(img);
    CHECK(decode_operation(s) == MaconOperation::Heating);
    macon_field_set(img, *macon_field_find("defrost_on"), 1);
    CHECK(decode_operation(master_view(img)) == MaconOperation::Defrost);
}

static void test_baseline_presence_parity() {
    // A fully-initialised simulator image must decode identically locally and
    // through the wire, including validity flags (bench reviewer finding #5).
    MaconImage img;
    img.fill_baseline();
    const MaconState a = local_view(img);
    const MaconState b = master_view(img);
    for (size_t i = 0; i < MACON_FIELDS_COUNT; ++i) {
        int32_t va = 0, vb = 0;
        const bool oka = macon_field_get(a, MACON_FIELDS[i], &va);
        const bool okb = macon_field_get(b, MACON_FIELDS[i], &vb);
        if (oka != okb || va != vb) std::printf("  parity %s\n", MACON_FIELDS[i].name);
        CHECK(oka == okb);
        CHECK(va == vb);
    }
    CHECK(a.faults_valid && b.faults_valid);

    // Without the baseline the fields are absent (MaconImage contract).
    MaconImage empty;
    CHECK(!local_view(empty).outlet_valid);
}

static void test_read_window() {
    MaconImage img;
    uint8_t buf[256];
    CHECK(img.read_window(0, 50, buf, sizeof(buf)) == 50);
    CHECK(img.read_window(50, 58, buf, sizeof(buf)) == 58);
    CHECK(img.read_window(0, 49, buf, sizeof(buf)) == 0);   // unknown window
    CHECK(img.read_window(0, 50, buf, 10) == 0);             // cap too small
    CHECK(img.read_window(0, 50, nullptr, 64) == 0);
}

static void test_apply_write() {
    MaconImage img;
    img.fill_baseline();
    // Hot-water setpoint 38 at wire 0x0002 (REGISTERS.md example).
    const uint8_t hw[1] = { 38 };
    CHECK(img.apply_write(0x0002, hw, 1));
    CHECK(local_view(img).hot_water_setpoint == 38);
    // Working mode Auto (6) at wire 0x0003 (OEM-captured value, arctic-macon #31).
    const uint8_t mode[1] = { 6 };
    CHECK(img.apply_write(0x0003, mode, 1));
    CHECK(master_view(img).working_mode == MaconWorkingMode::Auto);
    // Cooling setpoint -5 (signed byte).
    const uint8_t cool[1] = { static_cast<uint8_t>(-5) };
    CHECK(img.apply_write(0x0000, cool, 1));
    CHECK(master_view(img).cooling_setpoint == -5);
    // A write into the holding window lands there too.
    const uint8_t ceil[1] = { 55 };
    CHECK(img.apply_write(50 + 12, ceil, 1));   // wire 62 = reg2012
    CHECK(local_view(img).hot_water_ceiling == 55);

    // Outside every window (or straddling the end) -> rejected, image untouched.
    uint16_t before[MaconImage::window_count()], after[MaconImage::window_count()];
    uint16_t base = 0;
    img.raw(&base, before, MaconImage::window_count());
    const uint8_t two[2] = { 1, 2 };
    CHECK(!img.apply_write(200, hw, 1));
    CHECK(!img.apply_write(107, two, 2));   // byte 107 in, 108 out
    CHECK(!img.apply_write(0, nullptr, 1));
    img.raw(&base, after, MaconImage::window_count());
    CHECK(std::memcmp(before, after, sizeof(before)) == 0);
}

static void test_fault_catalog() {
    size_t total_sites = 0;
    for (uint16_t i = 0; i < static_cast<uint16_t>(MaconFaultId::Count); ++i) {
        const MaconFaultId id = static_cast<MaconFaultId>(i);
        MaconFaultSiteId sites[4];
        const size_t n = macon_fault_sites_for_id(id, sites, 4);
        CHECK(n >= 1);
        CHECK(macon_code_for_fault_id(id) != nullptr);
        total_sites += n;
        for (size_t k = 0; k < n; ++k) {
            // Lighting exactly one site yields exactly one active fault, with that
            // site's identity, when the master decodes the served windows.
            MaconImage img;
            img.fill_baseline();
            CHECK(img.set_fault_site(sites[k], true));
            const MaconState s = master_view(img);
            MaconFault out[8];
            const size_t nf = macon_decode_faults(s.fault_run, s.fault_ee, s.fault_comp,
                                                  s.fault_elec, s.fault_ref, out, 8);
            CHECK(nf == 1);
            if (nf == 1) {
                CHECK(out[0].site == sites[k]);
                CHECK(out[0].id == id);
            }
            CHECK(decode_operation(s) == MaconOperation::Fault);
            CHECK(img.set_fault_site(sites[k], false));
            const MaconState c = master_view(img);
            CHECK(!macon_has_fault(c.fault_run, c.fault_ee, c.fault_comp, c.fault_elec, c.fault_ref));
        }
    }
    // Every non-INFO table row is reachable through the catalog exactly once.
    size_t fault_rows = 0;
    for (size_t i = 0; i < MACON_FAULT_BITS_COUNT; ++i) {
        if (MACON_FAULT_BITS[i].severity != FaultSeverity::INFO) ++fault_rows;
    }
    CHECK(total_sites == fault_rows);
    CHECK(macon_fault_sites_for_id(MaconFaultId::EepromError, nullptr, 0) == 2);
    CHECK(macon_fault_sites_for_id(MaconFaultId::CoilSensor, nullptr, 0) == 2);
    CHECK(macon_fault_sites_for_id(MaconFaultId::Unknown, nullptr, 0) == 0);

    // The RUN indicator is not injectable as a fault site.
    MaconImage img;
    CHECK(!img.set_fault_site(macon_fault_site_id(2007, 5), true));
    CHECK(!img.set_fault_site(0, true));

    CHECK(std::strcmp(macon_fault_severity_name(FaultSeverity::FAULT), "error") == 0);
    CHECK(std::strcmp(macon_fault_severity_name(FaultSeverity::CRITICAL), "critical") == 0);
    CHECK(std::strcmp(macon_fault_severity_name(FaultSeverity::WARNING), "warning") == 0);
    CHECK(std::strcmp(macon_fault_severity_name(FaultSeverity::INFO), "info") == 0);
}

static void test_fingerprints() {
    const uint32_t l = macon_layout_fingerprint();
    const uint32_t c = macon_catalog_fingerprint();
    CHECK(l != 0 && c != 0 && l != c);
    CHECK(macon_layout_fingerprint() == l);    // deterministic
    CHECK(macon_catalog_fingerprint() == c);
    std::printf("layout fingerprint  %08lx\ncatalog fingerprint %08lx\n",
                (unsigned long)l, (unsigned long)c);
}

int main() {
    test_field_lookup();
    test_number_round_trip();
    test_strict_rejects();
    test_flags_and_enums();
    test_compressor_running_is_frequency();
    test_baseline_presence_parity();
    test_read_window();
    test_apply_write();
    test_fault_catalog();
    test_fingerprints();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("semantic: all tests passed\n");
    return 0;
}
