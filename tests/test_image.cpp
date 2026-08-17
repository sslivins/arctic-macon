// Native unit test for the opaque MaconImage: ingest, semantic setters,
// presence tracking, raw passthrough, decode. Framework-free.

#include "macon_image.h"
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

int main() {
    // --- fresh image decodes as entirely absent -----------------------------
    {
        MaconImage img;
        MaconState s;
        img.decode(&s);
        CHECK(!s.water_tank_valid);
        CHECK(!s.faults_valid);
        CHECK(!s.compressor_freq_valid);
        CHECK(s.mode == MaconMode::Unknown);
    }

    // --- set_temp round-trips through decode (incl. negative) ---------------
    {
        MaconImage img;
        CHECK(img.set_temp(MaconField::WaterTankTemp, 45) == MaconSetResult::Ok);
        CHECK(img.set_temp(MaconField::InletWaterTemp, -8) == MaconSetResult::Ok);
        MaconState s;
        img.decode(&s);
        CHECK(s.water_tank_valid && s.water_tank_c == 45);
        CHECK(s.inlet_valid && s.inlet_c == -8);
        // Presence: an unset temp stays invalid.
        CHECK(!s.outlet_valid);
        // Out-of-range clamps + reports truncation.
        CHECK(img.set_temp(MaconField::IpmTemp, 200) == MaconSetResult::Truncated);
    }

    // --- set_value scaling (volts /10, watts /100, amps x1) -----------------
    {
        MaconImage img;
        CHECK(img.set_value(MaconField::AcVoltage, 230) == MaconSetResult::Ok);
        CHECK(img.set_value(MaconField::DcVoltage, 380) == MaconSetResult::Ok);
        CHECK(img.set_value(MaconField::AcCurrent, 5)   == MaconSetResult::Ok);
        CHECK(img.set_value(MaconField::RealtimePower, 1200) == MaconSetResult::Ok);
        CHECK(img.set_value(MaconField::CompressorFreq, 60)  == MaconSetResult::Ok);
        MaconState s;
        img.decode(&s);
        CHECK(s.ac_voltage_valid && s.ac_voltage == 230);
        CHECK(s.dc_voltage_valid && s.dc_voltage == 380);
        CHECK(s.ac_current_valid && s.ac_current == 5);
        CHECK(s.realtime_power_valid && s.realtime_power_w == 1200);
        CHECK(s.compressor_freq_valid && s.compressor_freq == 60);
        // Non-multiple of the scale => Truncated, decodes to the floor.
        CHECK(img.set_value(MaconField::AcVoltage, 231) == MaconSetResult::Truncated);
        img.decode(&s);
        CHECK(s.ac_voltage == 230);
    }

    // --- flags + working mode -----------------------------------------------
    {
        MaconImage img;
        img.set_flag(MaconFlag::Pump, true);
        img.set_flag(MaconFlag::Fan, true);
        img.set_flag(MaconFlag::Cooling, true);
        img.set_flag(MaconFlag::UnitOn, true);
        img.set_working_mode(MaconWorkingMode::Auto);
        MaconState s;
        img.decode(&s);
        CHECK(s.pump_on && s.fan_on && s.cooling_on && s.running);
        CHECK(s.working_mode_valid && s.working_mode == MaconWorkingMode::Auto);
        img.set_flag(MaconFlag::Pump, false);
        img.decode(&s);
        CHECK(!s.pump_on && s.fan_on);   // clearing pump leaves fan set
    }

    // --- fault inject by id + by code; state predicate ----------------------
    {
        MaconImage img;
        img.set_fault(MaconFaultId::HighPressureProtection, true);
        MaconState s;
        img.decode(&s);
        CHECK(macon_state_has_fault(s, MaconFaultId::HighPressureProtection));
        CHECK(!macon_state_has_fault(s, MaconFaultId::InletWaterSensor));
        MaconFault out[8];
        size_t n = macon_decode_faults(s.fault_run, s.fault_ee, s.fault_comp,
                                       s.fault_elec, s.fault_ref, out, 8);
        CHECK(n == 1 && out[0].id == MaconFaultId::HighPressureProtection);

        // by-code lights both E28 sites.
        CHECK(img.set_fault_by_code("E28", true) == 2);
        img.decode(&s);
        CHECK(macon_state_has_fault(s, MaconFaultId::EepromError));

        img.set_fault(MaconFaultId::HighPressureProtection, false);
        img.decode(&s);
        CHECK(!macon_state_has_fault(s, MaconFaultId::HighPressureProtection));
    }

    // --- clear_faults preserves the RUN indicator ---------------------------
    {
        MaconImage img;
        img.set_flag(MaconFlag::UnitOn, true);                 // reg2007 bit5
        img.set_fault(MaconFaultId::HighPressureProtection, true);
        img.clear_faults();
        MaconState s;
        img.decode(&s);
        CHECK(s.running);                                      // RUN preserved
        CHECK(!macon_state_has_fault(s, MaconFaultId::HighPressureProtection));
    }

    // --- ingest equivalence with decode_state over the same bytes -----------
    {
        const uint16_t base = HOLDING_START;
        const size_t count = INPUT_START + INPUT_COUNT - HOLDING_START;  // 2000..2142
        uint16_t regs[200] = {0};
        // sprinkle representative values
        regs[REG_WATER_TANK_TEMP - base] = 44;
        regs[REG_COMPRESSOR_FREQ - base] = 55;
        regs[REG_OPERATING_MODE  - base] = 0x04;  // cooling
        regs[REG_FAULT_ELEC      - base] = 0x80;  // P02

        MaconImage img;
        MaconCoverage cov = img.ingest(base, regs, count);
        CHECK(cov.status_updated && cov.telemetry_updated);
        MaconState a, b;
        img.decode(&a);
        decode_state(base, regs, count, &b);       // present=nullptr (all present)
        CHECK(a.water_tank_c == b.water_tank_c && a.water_tank_c == 44);
        CHECK(a.compressor_freq == b.compressor_freq);
        CHECK(a.mode == b.mode && a.mode == MaconMode::Cooling);
        CHECK(macon_state_has_fault(a, MaconFaultId::HighPressureProtection));

        // A telemetry-only window does not claim status coverage.
        MaconImage img2;
        MaconCoverage cov2 = img2.ingest(INPUT_START, regs, 5);  // 2093..2097
        CHECK(!cov2.status_updated);
    }

    // --- raw passthrough + register poke/peek + window bounds ---------------
    {
        MaconImage img;
        img.set_temp(MaconField::WaterTankTemp, 30);
        uint16_t base_out = 0, buf[200] = {0};
        uint16_t n = img.raw(&base_out, buf, 200);
        CHECK(base_out == HOLDING_START);
        CHECK(n == MaconImage::window_count());
        CHECK(buf[REG_WATER_TANK_TEMP - HOLDING_START] == 30);

        CHECK(img.set_register(REG_WATER_TANK_TEMP, 22));
        uint16_t v = 0;
        CHECK(img.get_register(REG_WATER_TANK_TEMP, &v) && v == 22);
        CHECK(!img.set_register(1999, 5));      // below window
        CHECK(!img.set_register(9999, 5));      // above window
        CHECK(!img.set_register(REG_WATER_TANK_TEMP, 300));  // > one byte
        CHECK(img.in_window(HOLDING_START) && !img.in_window(1999));
    }

    // --- macon_field_address (diagnostic dump address lookup) ---------------
    {
        CHECK(macon_field_address(MaconField::WaterTankTemp) == REG_WATER_TANK_TEMP);
        CHECK(macon_field_address(MaconField::AcVoltage)     == REG_AC_VOLTAGE);
        CHECK(macon_field_address(MaconField::RealtimePower) == REG_REALTIME_POWER);
        CHECK(macon_field_address(MaconField::CoolingSetpoint) == REG_COOLING_SETPOINT);
        // Every settable field must resolve to a non-zero address.
        const MaconField all[] = {
            MaconField::WaterTankTemp, MaconField::OutletWaterTemp, MaconField::InletWaterTemp,
            MaconField::DischargeTemp, MaconField::SuctionTemp, MaconField::OutdoorCoilTemp,
            MaconField::IndoorCoilTemp, MaconField::OutdoorAmbientTemp, MaconField::IpmTemp,
            MaconField::CompressorFreq, MaconField::FanLevel, MaconField::AcVoltage,
            MaconField::AcCurrent, MaconField::DcVoltage, MaconField::PrimaryEev,
            MaconField::RealtimePower, MaconField::CoolingSetpoint, MaconField::HeatingSetpoint,
            MaconField::HotWaterSetpoint, MaconField::HotWaterCeiling };
        for (MaconField f : all) CHECK(macon_field_address(f) != 0);
    }

    if (g_failures) {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("all image tests passed\n");
    return 0;
}
