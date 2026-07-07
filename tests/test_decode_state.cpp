// Native unit test for the Macon decoded-state model (decode_mode + decode_state).
// Framework-free: prints failures and returns non-zero on any failure.

#include "macon_state.h"
#include "macon_registers.h"

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

    // --- decode_state: heating snapshot ------------------------------------
    uint16_t regs[COUNT];
    std::memset(regs, 0, sizeof(regs));
    set_reg(regs, REG_OPERATING_MODE, 0);       // heating
    set_reg(regs, REG_FAULT_RUNSTATE, 0x20);    // running
    set_reg(regs, REG_STATUS_BYTE, 0x04 | 0x08);// compressor + pump
    set_reg(regs, REG_ICON_BITS2, 0x10);        // fan on, no defrost
    set_reg(regs, REG_DC_MOTOR_SPEED, 45);      // fan level
    set_reg(regs, REG_OUTLET_WATER_TEMP, 45);
    set_reg(regs, REG_INLET_WATER_TEMP, 38);
    set_reg(regs, REG_OUTDOOR_AMBIENT_TEMP, (uint16_t)(uint8_t)(int8_t)-7); // sub-zero
    set_reg(regs, REG_DISCHARGE_TEMP, 85);
    set_reg(regs, REG_HOT_WATER_SETPOINT, 50);
    set_reg(regs, REG_AC_CURRENT, 12);
    set_reg(regs, REG_AC_VOLTAGE, 23);
    set_reg(regs, REG_DC_BUS_VOLTAGE, 36);      // *10 => 360 V
    set_reg(regs, REG_COMPRESSOR_FREQ, 55);
    set_reg(regs, REG_FAULT, 0x80);             // P01 water flow bit

    MaconState st;
    DecodeStatus ds = decode_state(BASE, regs, COUNT, &st);

    CHECK(st.mode == MaconMode::Heating);
    CHECK(st.mode_valid);
    CHECK(st.running);
    CHECK(st.compressor_on);
    CHECK(st.pump_on);
    CHECK(!st.defrost_on);
    CHECK(st.fan_on);
    CHECK(st.fan_level == 45);
    CHECK(st.outlet_c == 45);
    CHECK(st.inlet_c == 38);
    CHECK(st.outdoor_ambient_c == -7);          // signed byte decode
    CHECK(st.discharge_c == 85);
    CHECK(st.hot_water_setpoint == 50);
    CHECK(st.ac_current == 12);
    CHECK(st.ac_voltage == 23);
    CHECK(st.dc_voltage == 360);                // x10
    CHECK(st.compressor_freq == 55);
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
