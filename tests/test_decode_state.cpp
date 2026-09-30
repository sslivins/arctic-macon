// Native unit test for the Macon decoded-state model (decode_mode + decode_state).
// Framework-free: prints failures and returns non-zero on any failure.

#include "macon_state.h"
#include "macon_registers.h"

#include <cmath>
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

// Build a full holding+telemetry window (base 2000, 143 regs = 2000..2142).
static constexpr uint16_t BASE  = 2000;
static constexpr size_t   COUNT = 143;

static void set_reg(uint16_t regs[COUNT], uint16_t addr, uint16_t v) {
    regs[addr - BASE] = v;
}

int main() {
    // --- decode_mode: bit-masked, Unknown on unobserved bits ---------------
    CHECK(decode_mode(0x00) == MaconMode::Heating);
    CHECK(decode_mode(0x04) == MaconMode::Cooling);
    // stray/unconfirmed bits -> Unknown, never a silent guess
    CHECK(decode_mode(0x14) == MaconMode::Unknown);
    CHECK(decode_mode(0x01) == MaconMode::Unknown);
    CHECK(decode_mode(0x08) == MaconMode::Unknown);
    // high byte is masked off (wire register is one byte)
    CHECK(decode_mode(0x0400) == MaconMode::Heating);
    CHECK(decode_mode(0x0404) == MaconMode::Cooling);

    CHECK(std::strcmp(mode_name(MaconMode::Heating), "Heating") == 0);
    CHECK(std::strcmp(mode_name(MaconMode::Cooling), "Cooling") == 0);
    CHECK(std::strcmp(mode_name(MaconMode::Unknown), "Unknown") == 0);
    CHECK(decode_working_mode(0) == MaconWorkingMode::Cooling);
    CHECK(decode_working_mode(1) == MaconWorkingMode::Heating);
    CHECK(decode_working_mode(2) == MaconWorkingMode::Mode2);
    CHECK(decode_working_mode(3) == MaconWorkingMode::Mode3);
    CHECK(decode_working_mode(4) == MaconWorkingMode::Mode4);
    CHECK(decode_working_mode(5) == MaconWorkingMode::HotWater);
    CHECK(decode_working_mode(6) == MaconWorkingMode::HotWaterCooling);
    CHECK(decode_working_mode(7) == MaconWorkingMode::Unknown);
    CHECK(decode_working_mode(8) == MaconWorkingMode::Unknown);
    CHECK(std::strcmp(working_mode_name(MaconWorkingMode::Heating), "Heating") == 0);
    CHECK(std::strcmp(working_mode_name(MaconWorkingMode::Mode3), "Mode 3") == 0);
    CHECK(std::strcmp(working_mode_name(MaconWorkingMode::HotWater), "Hot water") == 0);
    CHECK(std::strcmp(working_mode_name(MaconWorkingMode::HotWaterCooling),
                      "Hot water / cooling") == 0);

    // --- decode_state: heating snapshot ------------------------------------
    uint16_t regs[COUNT];
    std::memset(regs, 0, sizeof(regs));
    set_reg(regs, REG_OPERATING_MODE, 0);       // heating
    set_reg(regs, REG_FAULT_RUNSTATE, 0x20);    // running
    set_reg(regs, REG_STATUS_BYTE, 0x04 | 0x08);// compressor + pump
    set_reg(regs, REG_ICON_BITS2, 0x10);        // fan on, no defrost
    set_reg(regs, REG_DC_MOTOR_SPEED, 45);      // raw fan level => 450 RPM (×10)
    set_reg(regs, REG_OUTLET_WATER_TEMP, 45);
    set_reg(regs, REG_INLET_WATER_TEMP, 38);
    set_reg(regs, REG_OUTDOOR_AMBIENT_TEMP, (uint16_t)(uint8_t)(int8_t)-7); // sub-zero
    set_reg(regs, REG_DISCHARGE_TEMP, 85);
    set_reg(regs, REG_HOT_WATER_CEILING, 50);   // reg2012 AP13 ceiling
    set_reg(regs, REG_COOLING_SETPOINT, 24);    // reg2093 cooling setpoint
    set_reg(regs, REG_HEATING_SETPOINT, 40);    // reg2094 heating setpoint
    set_reg(regs, REG_HOT_WATER_SETPOINT, 38);  // reg2095 live hot-water setpoint
    set_reg(regs, REG_WORKING_MODE, 1);         // heating
    set_reg(regs, REG_AC_CURRENT, 12);
    set_reg(regs, REG_AC_VOLTAGE, 23);          // *10 => 230 V
    set_reg(regs, REG_DC_BUS_VOLTAGE, 36);      // *10 => 360 V
    set_reg(regs, REG_COMPRESSOR_FREQ, 55);
    set_reg(regs, REG_REALTIME_POWER, 39);      // *100 => 3900 W
    set_reg(regs, REG_FAULT, 0x80);             // P01 water flow bit

    MaconState st;
    DecodeStatus ds = decode_state(BASE, regs, COUNT, &st);

    CHECK(st.mode == MaconMode::Heating);
    CHECK(st.mode_valid);
    CHECK(st.working_mode == MaconWorkingMode::Heating);
    CHECK(st.working_mode_valid);
    CHECK(st.running);
    CHECK(st.compressor_on);
    CHECK(st.pump_on);
    CHECK(!st.defrost_on);
    CHECK(st.fan_on);
    CHECK(st.fan_level == 450);   // raw 45 ×10 => RPM
    CHECK(st.fan_speed_max == 1000);   // library-provided assumed full scale
    CHECK(st.outlet_c == 45);
    CHECK(st.inlet_c == 38);
    CHECK(st.outdoor_ambient_c == -7);          // signed byte decode
    CHECK(st.discharge_c == 85);
    CHECK(st.hot_water_ceiling == 50);
    CHECK(st.hot_water_ceiling_valid);
    CHECK(st.hot_water_setpoint == 38);
    CHECK(st.hot_water_setpoint_valid);
    CHECK(st.heating_setpoint == 40);
    CHECK(st.heating_setpoint_valid);
    CHECK(st.cooling_setpoint == 24);
    CHECK(st.cooling_setpoint_valid);
    CHECK(st.ac_current == 12);
    CHECK(st.ac_voltage == 230);                // x10
    CHECK(st.dc_voltage == 360);                // x10
    CHECK(st.compressor_freq == 55);
    CHECK(st.realtime_power_w == 3900);         // x100
    CHECK(st.realtime_power_valid);
    CHECK(st.fault_ref == 0x80);
    CHECK(st.faults_valid);
    CHECK(ds.registers_present > 0);
    CHECK(ds.registers_expected >= ds.registers_present);

    // --- decode_state: cooling snapshot ------------------------------------
    std::memset(regs, 0, sizeof(regs));
    set_reg(regs, REG_OPERATING_MODE, 4);       // cooling
    MaconState cs;
    decode_state(BASE, regs, COUNT, &cs);
    CHECK(cs.mode == MaconMode::Cooling);
    CHECK(cs.mode_valid);
    CHECK(!cs.running);                          // run-state 0 -> not running

    // --- decode_state: register outside the window -> not valid ------------
    // A short window that does not reach reg2049.
    uint16_t small[10];
    std::memset(small, 0, sizeof(small));
    MaconState ss;
    decode_state(BASE, small, 10, &ss);          // covers 2000..2009 only
    CHECK(!ss.mode_valid);                        // reg2049 absent
    CHECK(ss.mode == MaconMode::Unknown);         // absent register -> Unknown, not Heating
    CHECK(ss.ac_current_valid);                   // reg2000 present
    CHECK(!ss.outlet_valid);                      // reg2132 absent

    // --- decode_operation --------------------------------------------------
    {
        uint16_t r[COUNT];
        auto reset = [&]() {
            std::memset(r, 0, sizeof(r));
            set_reg(r, REG_FAULT_RUNSTATE, 0x20);   // enabled, no fault
            set_reg(r, REG_COMPRESSOR_FREQ, 55);    // compressor running
            set_reg(r, REG_OPERATING_MODE, 0);      // heating direction
            set_reg(r, REG_WORKING_MODE, 1);        // floor heating request
            set_reg(r, REG_INLET_WATER_TEMP, 38);
            set_reg(r, REG_OUTLET_WATER_TEMP, 45);
        };
        MaconState o;

        reset();                                    // running + heating
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Heating);

        reset();                                    // running + cooling
        set_reg(r, REG_OPERATING_MODE, 4);
        set_reg(r, REG_WORKING_MODE, 0);
        set_reg(r, REG_ICON_BITS2, 0x14);
        set_reg(r, REG_INLET_WATER_TEMP, 13);
        set_reg(r, REG_OUTLET_WATER_TEMP, 10);
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Cooling);

        reset();                                    // Auto heating despite stale 2049=4
        set_reg(r, REG_OPERATING_MODE, 4);
        set_reg(r, REG_WORKING_MODE, 6);
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Heating);

        reset();                                    // Auto cooling follows live status
        set_reg(r, REG_OPERATING_MODE, 4);
        set_reg(r, REG_WORKING_MODE, 6);
        set_reg(r, REG_ICON_BITS2, 0x14);
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Cooling);

        reset();                                    // run bit may coexist with another flag
        set_reg(r, REG_FAULT_RUNSTATE, 0x60);
        decode_state(BASE, r, COUNT, &o);
        CHECK(o.running);

        reset();                                    // enabled, compressor stopped
        set_reg(r, REG_COMPRESSOR_FREQ, 0);
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Idle);

        reset();                                    // defrost outranks direction
        set_reg(r, REG_ICON_BITS2, 0x02);
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Defrost);

        reset();                                    // not enabled -> Off
        set_reg(r, REG_FAULT_RUNSTATE, 0x00);
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Off);

        reset();                                    // active fault outranks Off
        set_reg(r, REG_FAULT_RUNSTATE, 0x00);
        set_reg(r, REG_FAULT, 0x80);
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Fault);

        reset();                                    // reg2007 low-nibble fault bit
        set_reg(r, REG_FAULT_RUNSTATE, 0x21);       // enabled + P15
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Fault);

        reset();                                    // reg2130 icon bit must NOT gate
        set_reg(r, REG_COMPRESSOR_FREQ, 0);         // not running by frequency
        set_reg(r, REG_STATUS_BYTE, 0x04);          // icon compressor bit set
        decode_state(BASE, r, COUNT, &o);
        CHECK(decode_operation(o) == MaconOperation::Idle);

        // No status registers decoded -> Unknown.
        uint16_t few[10];
        std::memset(few, 0, sizeof(few));
        MaconState u;
        decode_state(BASE, few, 10, &u);            // covers 2000..2009 only
        CHECK(decode_operation(u) == MaconOperation::Unknown);

        CHECK(std::strcmp(operation_name(MaconOperation::Heating), "Heating") == 0);
        CHECK(std::strcmp(operation_name(MaconOperation::Idle), "Idle") == 0);
        CHECK(std::strcmp(operation_name(MaconOperation::Off), "Off") == 0);
    }

    // --- encode_cooling_setpoint: byte-exact vs the live capture -----------
    // Live 2026-07-08: dialing to 24 °C emitted `55 AA F0 06 00 00 00 01 18 F0`.
    {
        uint8_t buf[16];
        size_t n = encode_cooling_setpoint(buf, sizeof(buf), 24);
        CHECK(n == 10);
        const uint8_t expected[10] =
            { 0x55, 0xAA, 0xF0, 0x06, 0x00, 0x00, 0x00, 0x01, 0x18, 0xF0 };
        CHECK(std::memcmp(buf, expected, 10) == 0);

        // A too-small buffer must fail cleanly (return 0).
        CHECK(encode_cooling_setpoint(buf, 4, 24) == 0);
    }

    // --- estimate_performance ----------------------------------------------
    {
        const PerformanceInputs water40 = {
            /*water_flow_lpm=*/40.0f, /*cp=*/4186.0f, /*density=*/1.00f };

        // Heating: outlet 45, inlet 38 -> dT +7 °C; power 3900 W.
        uint16_t r[COUNT];
        std::memset(r, 0, sizeof(r));
        set_reg(r, REG_OPERATING_MODE, 0);       // heating
        set_reg(r, REG_COMPRESSOR_FREQ, 55);     // running
        set_reg(r, REG_OUTLET_WATER_TEMP, 45);
        set_reg(r, REG_INLET_WATER_TEMP, 38);
        set_reg(r, REG_REALTIME_POWER, 39);      // x100 => 3900 W
        MaconState h;
        decode_state(BASE, r, COUNT, &h);
        PerformanceEstimate eh = estimate_performance(h, water40);
        CHECK(eh.valid);
        CHECK(eh.direction == MaconMode::Heating);
        // Q = (40/60)*4186*7 = 19534.7 W
        CHECK(eh.thermal_w > 19500 && eh.thermal_w < 19570);
        // COP = 19534.7 / 3900 = 5.01
        CHECK(eh.cop_x100 > 495 && eh.cop_x100 < 505);

        // Cooling: outlet 12, inlet 14 -> dT -2 °C; power 1400 W.
        std::memset(r, 0, sizeof(r));
        set_reg(r, REG_OPERATING_MODE, 4);       // cooling
        set_reg(r, REG_COMPRESSOR_FREQ, 30);     // running
        set_reg(r, REG_OUTLET_WATER_TEMP, 12);
        set_reg(r, REG_INLET_WATER_TEMP, 14);
        set_reg(r, REG_REALTIME_POWER, 14);      // x100 => 1400 W
        MaconState c;
        decode_state(BASE, r, COUNT, &c);
        PerformanceEstimate ec = estimate_performance(c, water40);
        CHECK(ec.valid);
        CHECK(ec.direction == MaconMode::Cooling);
        CHECK(ec.thermal_w < 0);                 // heat removed -> negative
        // |Q| = (40/60)*4186*2 = 5581.3 W ; COP = 5581.3/1400 = 3.99
        CHECK(ec.thermal_w > -5620 && ec.thermal_w < -5540);
        CHECK(ec.cop_x100 > 393 && ec.cop_x100 < 405);

        // Not running -> invalid.
        set_reg(r, REG_COMPRESSOR_FREQ, 0);
        MaconState off;
        decode_state(BASE, r, COUNT, &off);
        CHECK(!estimate_performance(off, water40).valid);

        // dT == 0 -> invalid (below whole-°C noise floor).
        std::memset(r, 0, sizeof(r));
        set_reg(r, REG_COMPRESSOR_FREQ, 40);
        set_reg(r, REG_OUTLET_WATER_TEMP, 30);
        set_reg(r, REG_INLET_WATER_TEMP, 30);
        set_reg(r, REG_REALTIME_POWER, 20);
        MaconState flat;
        decode_state(BASE, r, COUNT, &flat);
        CHECK(!estimate_performance(flat, water40).valid);

        // Zero/negative flow input -> invalid.
        const PerformanceInputs noflow = { 0.0f, 4186.0f, 1.0f };
        CHECK(!estimate_performance(h, noflow).valid);

        // Defrosting -> invalid (the unit is pulling heat back out of the loop).
        MaconState hd = h;
        hd.defrost_on = true;
        CHECK(!estimate_performance(hd, water40).valid);

        // --- estimate_performance_with_temps (external sensors) ----------
        // Heating, supply 41.25 / return 37.75 -> dT +3.5 K, power 3900 W.
        PerformanceEstimate xh = estimate_performance_with_temps(h, 41.25f, 37.75f, water40);
        CHECK(xh.valid);
        // Q = (40/60)*4186*3.5 = 9767.3 W ; COP = 2.50
        CHECK(xh.thermal_w > 9740 && xh.thermal_w < 9790);
        CHECK(xh.cop_x100 >= 249 && xh.cop_x100 <= 251);

        // Cooling, supply 11.4 / return 13.9 -> dT -2.5 K, power 1400 W.
        PerformanceEstimate xc = estimate_performance_with_temps(c, 11.4f, 13.9f, water40);
        CHECK(xc.valid);
        CHECK(xc.thermal_w < 0);
        // |Q| = (40/60)*4186*2.5 = 6976.7 W ; COP = 4.98
        CHECK(xc.cop_x100 >= 496 && xc.cop_x100 <= 500);

        // Wrong direction for the mode (sensors swapped) -> invalid.
        CHECK(!estimate_performance_with_temps(h, 37.75f, 41.25f, water40).valid);
        CHECK(!estimate_performance_with_temps(c, 13.9f, 11.4f, water40).valid);

        // Below the noise floor -> invalid; custom floor honoured.
        CHECK(!estimate_performance_with_temps(h, 40.2f, 40.0f, water40).valid);
        CHECK(estimate_performance_with_temps(h, 40.2f, 40.0f, water40, 0.1f).valid);

        // Doesn't need the unit's own water sensors.
        MaconState hn = h;
        hn.inlet_valid = false;
        hn.outlet_valid = false;
        CHECK(!estimate_performance(hn, water40).valid);
        CHECK(estimate_performance_with_temps(hn, 41.25f, 37.75f, water40).valid);

        // Still gated on compressor, defrost and NaN.
        CHECK(!estimate_performance_with_temps(off, 41.25f, 37.75f, water40).valid);
        CHECK(!estimate_performance_with_temps(hd, 41.25f, 37.75f, water40).valid);
        CHECK(!estimate_performance_with_temps(h, NAN, 37.75f, water40).valid);
    }

    // --- setpoint limits ---------------------------------------------------
    {
        // Cooling: library-owned static range, never from the unit.
        SetpointLimits cl = setpoint_limits(SetpointKind::Cooling);
        CHECK(cl.min_c == 4 && cl.max_c == 30 && !cl.max_from_unit);
        CHECK(clamp_setpoint(SetpointKind::Cooling, 3)  == 4);    // below min -> min
        CHECK(clamp_setpoint(SetpointKind::Cooling, 4)  == 4);    // lower bound accepted
        CHECK(clamp_setpoint(SetpointKind::Cooling, 40) == 30);   // above max -> max
        CHECK(clamp_setpoint(SetpointKind::Cooling, 22) == 22);   // in range -> unchanged

        // Heating: static range.
        SetpointLimits hl = setpoint_limits(SetpointKind::Heating);
        CHECK(hl.min_c == 20 && hl.max_c == 60 && !hl.max_from_unit);

        // Hot water, no state -> static default max.
        SetpointLimits wl = setpoint_limits(SetpointKind::HotWater);
        CHECK(wl.min_c == 30 && wl.max_c == 60 && !wl.max_from_unit);

        // Hot water WITH a live ceiling (reg2012) -> max tracks the unit.
        uint16_t rr[COUNT] = {0};
        set_reg(rr, REG_HOT_WATER_CEILING, 50);
        MaconState wc;
        decode_state(BASE, rr, COUNT, &wc);
        SetpointLimits wcl = setpoint_limits(SetpointKind::HotWater, &wc);
        CHECK(wcl.min_c == 30 && wcl.max_c == 50 && wcl.max_from_unit);
        CHECK(clamp_setpoint(SetpointKind::HotWater, 55, &wc) == 50);  // clamped to live ceiling
    }

    // --- null-safety -------------------------------------------------------
    DecodeStatus dz = decode_state(BASE, nullptr, 0, &ss);
    CHECK(dz.registers_present == 0);
    (void)decode_state(BASE, regs, COUNT, nullptr);  // must not crash

    if (g_failures == 0) {
        std::printf("all decode-state tests passed\n");
        return 0;
    }
    std::printf("%d decode-state test(s) FAILED\n", g_failures);
    return 1;
}
