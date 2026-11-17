// Conformance tests: pin the library against REAL OEM wire bytes and
// hand-derived expectations, so a simulator and a controller that both link
// this library can't silently agree on a wrong mapping.
//
// Provenance:
//   * kRaw* frames: verbatim lines from arctic-simulator
//     tests/data/capture_raw.jsonl (OEM Macon mainboard <-> OEM wall
//     controller, captured 2026-05-03, unit idle in hot-water mode). Expected
//     values were decoded by hand from the payload bytes, NOT via this library.
//   * fc06 goldens: live OEM-controller writes (cooling 24 °C, 2026-07-08) and
//     the REGISTERS.md hot-water example (50 -> 38).
//   * Fault table: the ground-truthed bit map in docs/REGISTERS.md. Any change
//     to a (reg, bit, code, severity) row must be a deliberate edit here too.

#include "macon_faults.h"
#include "macon_image.h"
#include "macon_state.h"
#include "tuya_codec.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace arctic;
using namespace tuya_codec;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// t=16116: holding-window poll + response (tank 20 °C).
static const char *kRawHolding =
    "55aaf0030032003aa055aa0f030032003a002000000000002014232d2d32fa0300000000"
    "00042d001c1205050000f63cf90a2d050c5f0fe232ff0a0a050205ff0000000014f10000"
    "000008f800";
// t=17112: telemetry-window poll + response.
static const char *kRawTelemetry =
    "55aaf00300000032da55aa0f03000000320a28320501000f1e17060911230020231e2800"
    "000c0001000000000000000060000000000000000a0a0f0b0b0f0e001300e68514";

static std::vector<uint8_t> unhex(const char *s) {
    std::vector<uint8_t> out;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(std::string(s + i, 2), nullptr, 16)));
    }
    return out;
}

// Walk a raw capture blob frame by frame; ingest every read response.
static size_t ingest_capture(MaconImage &img, const char *hex, int *requests) {
    const std::vector<uint8_t> b = unhex(hex);
    size_t pos = 0, responses = 0;
    while (pos < b.size()) {
        const size_t start = pos + find_frame_start(b.data() + pos, b.size() - pos);
        if (start >= b.size()) break;
        ParsedFrame f{};
        if (parse_frame(b.data() + start, b.size() - start, f) != ParseResult::OK) {
            pos = start + 1;
            continue;
        }
        if (f.dir == DIR_REQUEST) {
            ++*requests;
        } else if (f.payload) {
            img.ingest_bytes(f.window->field_a + f.window->prefix_len, f.payload + f.window->prefix_len,
                             f.payload_len - f.window->prefix_len);
            ++responses;
        }
        pos = start + f.frame_len;
    }
    return responses;
}

static void test_real_capture_decode() {
    MaconImage img;
    int requests = 0;
    CHECK(ingest_capture(img, kRawHolding, &requests) == 1);
    CHECK(ingest_capture(img, kRawTelemetry, &requests) == 1);
    CHECK(requests == 2);

    MaconState s;
    img.decode(&s);
    // Telemetry window (@0..@49).
    CHECK(s.cooling_setpoint_valid && s.cooling_setpoint == 10);   // @0 = 0x0a
    CHECK(s.heating_setpoint_valid && s.heating_setpoint == 40);   // @1 = 0x28
    CHECK(s.hot_water_setpoint_valid && s.hot_water_setpoint == 50); // @2 = 0x32
    CHECK(s.working_mode_valid && s.working_mode == MaconWorkingMode::HotWater); // @3 = 5
    CHECK(s.ac_voltage_valid && s.ac_voltage == 230);              // @49 = 230 (x1)
    CHECK(s.primary_eev_valid && s.primary_eev == 190);            // @47 = 19 (x10 => steps)
    CHECK(s.ipm_valid && s.ipm_c == 12);                           // @20
    CHECK(s.realtime_power_valid && s.realtime_power_w == 0);      // @21
    CHECK(s.outlet_valid && s.outlet_c == 10);                     // @39
    CHECK(s.inlet_valid && s.inlet_c == 10);                       // @40
    CHECK(s.outdoor_ambient_valid && s.outdoor_ambient_c == 15);   // @41
    CHECK(s.indoor_coil_valid && s.indoor_coil_c == 11);           // @42
    CHECK(s.outdoor_coil_valid && s.outdoor_coil_c == 11);         // @43
    CHECK(s.suction_valid && s.suction_c == 15);                   // @44
    CHECK(s.discharge_valid && s.discharge_c == 14);               // @45
    CHECK(s.compressor_freq_valid && s.compressor_freq == 0);      // @48
    // Holding window: refrigerant pressures (no transducers fitted) + aux EEV.
    CHECK(s.high_pressure_valid && s.high_pressure == -6);         // @63 = 0xfa
    CHECK(s.low_pressure_valid && s.low_pressure == 3);            // @64
    CHECK(s.aux_eev_valid && s.aux_eev == 0);                      // @98
    CHECK(!s.compressor_on && !s.pump_on && !s.defrost_on && !s.fan_on); // @36/@37 = 0
    // Holding window (@50..@107).
    CHECK(s.ac_current_valid && s.ac_current == 0);                // @50
    CHECK(s.dc_voltage_valid && s.dc_voltage == 320);              // @51 = 0x20 (x10)
    CHECK(s.fan_level == 0);                                       // @53
    CHECK(s.water_tank_valid && s.water_tank_c == 20);             // @58 = 0x14
    CHECK(s.hot_water_ceiling_valid && s.hot_water_ceiling == 50); // @62 = 0x32
    CHECK(s.mode_valid && s.mode == MaconMode::Heating);           // @99 = 0
    CHECK(s.running);                                              // @57 bit5
    // Faults: none, and the RUN bit is not reported as one.
    CHECK(s.faults_valid);
    CHECK(!macon_has_fault(s.fault_run, s.fault_ee, s.fault_comp, s.fault_elec, s.fault_ref));
    CHECK(decode_operation(s) == MaconOperation::Idle);

    // Serving the image back reproduces the exact OEM payload bytes.
    const std::vector<uint8_t> hold = unhex(kRawHolding);
    const std::vector<uint8_t> tele = unhex(kRawTelemetry);
    uint8_t buf[128];
    CHECK(img.read_window(50, 58, buf, sizeof(buf)) == 58);
    CHECK(std::memcmp(buf, hold.data() + 9 + 8, 58) == 0);    // after req(9) + resp hdr(8)
    CHECK(img.read_window(0, 50, buf, sizeof(buf)) == 50);
    CHECK(std::memcmp(buf, tele.data() + 9 + 8, 50) == 0);
    // ...and re-encoding produces the byte-identical OEM response frame.
    uint8_t frame[128];
    CHECK(img.read_window(50, 58, buf, sizeof(buf)) == 58);
    const size_t fl = encode_response(frame, sizeof(frame), FC_READ, 50, 58, buf);
    CHECK(fl == 58 + 9);
    CHECK(std::memcmp(frame, hold.data() + 9, fl) == 0);
}

static void test_fc06_goldens() {
    struct Golden { const char *hex; uint16_t addr; uint8_t value; };
    const Golden goldens[] = {
        { "55aaf00600000001" "18" "f0", 0x0000, 24 },   // live capture, cooling 24
        { "55aaf00600020001" "26" "e0", 0x0002, 38 },   // REGISTERS.md hot water 50->38
    };
    for (const Golden &g : goldens) {
        const std::vector<uint8_t> want = unhex(g.hex);
        uint8_t buf[16];
        const size_t n = encode_command(buf, sizeof(buf), g.addr, 1, &g.value);
        CHECK(n == want.size());
        CHECK(std::memcmp(buf, want.data(), want.size()) == 0);
        // A slave applying the parsed frame updates the matching setpoint.
        ParsedFrame f{};
        CHECK(parse_frame(want.data(), want.size(), f) == ParseResult::OK);
        MaconImage img;
        img.fill_baseline();
        CHECK(img.apply_write(f.field_a, f.payload, f.payload_len));
        MaconState s;
        img.decode(&s);
        CHECK((g.addr == 0 ? s.cooling_setpoint : s.hot_water_setpoint) == g.value);
    }
}

static void test_fault_table_pinned() {
    struct Row { uint16_t reg; uint8_t bit; const char *code; FaultSeverity sev; };
    using S = FaultSeverity;
    const Row expected[] = {
        {57,0,"P15",S::WARNING}, {57,1,"P16",S::WARNING}, {57,2,"FE",S::FAULT},
        {57,3,"FF",S::FAULT},    {57,5,"RUN",S::INFO},
        {32,0,"E28",S::FAULT},   {32,1,"E19",S::FAULT},   {32,2,"E18",S::FAULT},
        {32,3,"E13",S::FAULT},   {32,4,"E03",S::FAULT},   {32,5,"E28",S::FAULT},
        {32,6,"E27",S::CRITICAL},{32,7,"E21",S::CRITICAL},
        {33,0,"r02",S::FAULT},   {33,1,"E26",S::CRITICAL},{33,2,"r01",S::CRITICAL},
        {33,4,"E01",S::FAULT},   {33,5,"E09",S::FAULT},   {33,6,"E05",S::FAULT},
        {33,7,"E22",S::FAULT},
        {34,0,"FA",S::FAULT},    {34,1,"P19",S::FAULT},   {34,2,"r06",S::FAULT},   {34,3,"r10",S::FAULT},
        {34,4,"r11",S::FAULT},   {34,5,"r05",S::FAULT},   {34,6,"P11",S::FAULT},
        {34,7,"P02",S::CRITICAL},
        {35,0,"P06",S::CRITICAL},{35,1,"P27",S::FAULT},   {35,2,"PC",S::WARNING},
        {35,3,"P10",S::FAULT},   {35,4,"P30",S::WARNING}, {35,5,"E05",S::FAULT},
        {35,7,"P01",S::CRITICAL},
    };
    const size_t n = sizeof(expected) / sizeof(expected[0]);
    CHECK(MACON_FAULT_BITS_COUNT == n);
    for (size_t i = 0; i < n && i < MACON_FAULT_BITS_COUNT; ++i) {
        const auto &a = MACON_FAULT_BITS[i];
        const Row &e = expected[i];
        const bool ok = a.reg == e.reg && a.bit == e.bit &&
                        std::strcmp(a.code, e.code) == 0 && a.severity == e.sev;
        if (!ok) std::printf("  fault row %zu: %u.%u %s\n", i, a.reg, a.bit, a.code);
        CHECK(ok);
    }
}

int main() {
    test_real_capture_decode();
    test_fc06_goldens();
    test_fault_table_pinned();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("conformance: all tests passed\n");
    return 0;
}
