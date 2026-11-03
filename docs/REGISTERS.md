# Macon register map

Canonical, reverse-engineered map of the **Arctic (Macon)** heat-pump Tuya-MCU
RS-485 bus. This is the single source of truth that `macon_registers.{h,cpp}`
and `macon_state.{h,cpp}` implement; keep it in sync when a register is
confirmed or corrected.

> **Provenance.** Every entry here was ground-truthed live on the real unit by
> correlating bus reads/writes against the OEM controller LCD, the Smart Life
> app, and the installer ("Cn"/"o"/"A") menu. Registers that have **not** been
> confirmed are marked **UNVERIFIED** and must not be relied on.

## Wire format

```
55 AA <dir> <fc> <addr:2BE> <count:2BE> [data:count] <chk>
```

| field | meaning |
|-------|---------|
| `dir` | `0xF0` controller→unit (request), `0x0F` unit→controller (response/ACK) |
| `fc`  | `0x03` read block, `0x06` write single block (setpoints) |
| `addr`| **byte offset** into the unified register page (NOT a Modbus register number) |
| `count`| number of payload bytes |
| `chk` | `~(sum of bytes after 55AA up to but excluding chk) & 0xFF` |

Each wire "register" is **one byte** (not 2 like classic Modbus). After each
*response* the unit appends one trailing block-tag byte (`0x14` after the
telemetry window, `0x00` after the holding window) **outside** the frame; it is
safe to ignore (the codec resyncs past it).

### Register numbering

The library labels registers by a synthetic number so the two wire windows form
one contiguous space:

| wire window (`addr`,`count`) | library base | covers |
|------------------------------|--------------|--------|
| `0`, `50`  (telemetry) | `reg2093` | `reg2093 … reg2142` — byte *k* = `reg 2093+k` |
| `50`, `58` (holding)   | `reg2000` | `reg2000 … reg2057` — byte *k* = `reg 2000+k` |

So a wire write to `addr = N` in the telemetry window targets `reg 2093 + N`.

## Setpoints (fc=0x06 writes)  ← the important part

The OEM controller (bus master) SETS a setpoint by writing **one signed byte**
(whole °C) to the setpoint's wire byte offset with fc=0x06, and the mainboard
replies with a 9-byte ACK echoing `addr`+`count` (no data). Confirmed live
2026-07-09.

| function | wire addr | library reg | live value seen | status |
|----------|-----------|-------------|-----------------|--------|
| **Cooling setpoint** | `0x0000` | `reg2093` | 12 → 24 (dial), −5 min | **confirmed** |
| **Heating setpoint** (modes 1 and 2) | `0x0001` | `reg2094` | 40 | Confirmed on the bench 2026-09-26 (OEM wired controller wrote 47 here) |
| **Hot-water setpoint** (live target) | `0x0002` | `reg2095` | 50 → 38 (dial) | **confirmed** |

Example (hot-water 50 → 38 °C):

```
controller → unit : 55 AA F0 06 0002 0001 26 <chk>   (write reg2095 = 0x26 = 38)
unit → controller : 55 AA 0F 06 0002 0001 <chk>       (ACK, echoes addr+count, no data)
```

A non-exception ACK means **accepted + applied** (Modbus semantics), so the ACK
is the authoritative confirmation. A NACK/exception frame (`fc | 0x80`) has
**never been observed** on this unit — its exact shape is unconfirmed (tracked
as the `macon-nack-probe` follow-up). `MaconLink` detects a `0x86` best-effort.

### Not a live setpoint: the hot-water *ceiling*

| function | library reg | note |
|----------|-------------|------|
| **Hot-water ceiling** | `reg2012` (Cn13) | The manual's Cn13 = "Highest setting temperature of hot water, heating mode" (range 20~55 °C, default 50). This is the mainboard's enforced **ceiling**, NOT the live user setpoint. Dialing the display setpoint down did not move reg2012 (stayed at 50). |

`MaconLink` (`macon_link.{h,cpp}`) exposes the intent-named transactions:
`set_cooling_setpoint()` / `set_hot_water_setpoint()` / `set_working_mode()` (write + ACK) and
`read_cooling_setpoint()` / `read_hot_water_setpoint()` / `read_heating_setpoint()`
(reg2094, untested).

## Telemetry window (`reg2093 … reg2142`)

| reg | code | field | scale / notes |
|-----|------|-------|---------------|
| 2093 | — | Cooling setpoint | signed °C (wire 0x0000) |
| 2094 | — | Heating setpoint | signed °C (wire 0x0001, writable; used by modes 1 and 2) |
| 2095 | — | Hot-water setpoint | signed °C (wire 0x0002) |
| 2096 | — | Selected working mode | 0 cooling, 1 heating, 5 hot water, 6 hot water / cooling (wire 0x0003, writable). 2/3/4 are heating variants, decoded but not writable: 2 = flashing heating icon with the 2094 setpoint, decoded as "Freeze protection" (`freeze_protection`) because the field units switch to it while idle whenever inlet/outlet water reaches 2.0 °C, run the pump ~30 s, then return to the selected mode; 3 =  solid icon with holding reg2010, 4 = flashing icon with holding reg2011. 7+ are out of range. |
| 2113 | A8  | IPM temp | signed °C |
| 2114 | A9  | Real-time power | ×100 = W |
| 2125 | — | Fault: sensor/EE/comm E-codes | bitfield |
| 2126 | — | Fault: sensor/comm/compressor | bitfield |
| 2127 | — | Fault: electrical/power-stage | bitfield |
| 2128 | — | Fault: refrigerant/protection (P-codes) | bitfield |
| 2129 | — | Icon bits #2 | bit1 defrost, bit4 fan |
| 2130 | — | Status | bit2 compressor *icon* (not "running" — use frequency > 0), bit3 pump |
| 2132 | o3  | Outlet (supply) water temp | signed °C |
| 2133 | o2  | Inlet (return) water temp | signed °C |
| 2134 | o4  | Outdoor ambient temp | signed °C |
| 2135 | A6  | Cool coil temp | signed °C |
| 2136 | A2  | Coil temp | signed °C |
| 2137 | A3  | Suction temp | signed °C |
| 2138 | A1  | Discharge temp | signed °C |
| 2140 | A5  | Main EEV opening (`primary_eev`) | Raw byte, shown as-is by the OEM (bench-verified 2026-11-03: wrote 37, OEM A5 = 37). 48 in cooling, 12–20 regulating in heating. ×10 = steps is plausible (manual opening AP50 is 0–500) but unconfirmed. The library labels the value as-shown in `steps`; see [EEV opening unit](#eev-opening-unit). Before 2026-11 the library read A5 from 2104, a constant setting byte. |
| 2141 | A14 | Compressor frequency | Hz |
| 2142 | A13 | AC input voltage (`ac_voltage`) | V ×1 (live 217–245). The OEM shows V ÷ 10, truncated (bench-verified 2026-11-03: 211 → A13 21, 187 → A13 18). Before 2026-11 the library read A13 from 2101 ×10, a constant setting byte that always gave 230 V. |

## Holding window (`reg2000 … reg2057`)

| reg | code | field | scale / notes |
|-----|------|-------|---------------|
| 2000 | A4  | AC input current | A |
| 2001 | A7  | DC bus voltage | ×10 = V |
| 2003 | A10 | DC (fan) motor speed | ×10 = RPM (tach-verified: raw 70 = 700 RPM) |
| 2007 | — | Run-state / fault code | `0x00` off, `0x20` hot-water RUN, low/high = P-faults |
| 2008 | o1  | Water tank temp | signed °C (real immersed DHW probe) |
| 2012 | Cn13 | Hot-water ceiling | see setpoints section |
| 2013 | A11 | High pressure (`high_pressure`) | Signed byte, shown as-is (bench-verified 2026-11-03: wrote 9, OEM A11 = 9; the live value 250 shows as −6). Unit and scale unknown. Constant −6 on both units, and every OEM log sent to Arctic shows A11 = −6, so no transducer is fitted. The library decodes it for units that have one; a UI should hide it by default. |
| 2014 | A12 | Low pressure (`low_pressure`) | Raw byte, shown as-is (bench-verified 2026-11-03: wrote 7, OEM A12 = 7). Unit and scale unknown. Constant 3 on both units and in every OEM log sent to Arctic, so no transducer is fitted. Decoded for units that have one; hide by default. |
| 2048 | o6  | Auxiliary (EVI) EEV opening (`aux_eev`) | Raw byte, shown as-is (bench-verified 2026-11-03: wrote 29, OEM o6 = 29; Arctic docs: "Auxiliary elec. expansion valve degree"). 0 almost always; opens only in some heating/idle conditions. Reported in `steps`; see [EEV opening unit](#eev-opening-unit). |
| 2049 | — | Operating direction | `0x00` heating, `0x04` cooling (reversing valve) |

The installer **Cn** advanced-parameter block (`reg = 2000 + Cn`, roughly
`reg2013 … reg2057`) is documented and guarded separately in
`macon_advanced_params.{h,cpp}` (validate-only, reject-not-clamp; no write path).

### EEV opening unit

`primary_eev` (A5) and `aux_eev` (o6) are reported with the unit `steps`, but
**that unit is not confirmed**. Arctic's docs call them the "main / auxiliary
elec. expansion valve degree"; "degree" is a translation of 开度 ("opening
degree"), meaning how far the valve is open, not an angle. Stepper-driven
EEVs are normally counted in motor steps (pulses) with ~480–500 for full
travel, which fits the 0–500 range of the manual-opening parameters AP50/AP51
(`reg2062`/`reg2064`). The wire byte tops out at 255, though, and the units read
12–20 heating and 48 cooling, so the displayed value may be steps ÷ 10. The
library reports the value exactly as the OEM display shows it.

To settle it: in manual EEV mode (AP48), set AP50 to a known opening (e.g. 300) and
see whether A5 shows 300, 30 or something else.

### Advanced-parameter quirks

Arctic's "Appendix C - Advanced parameter setting V4 (Cn31)" is the source for
the AP table, but it isn't reliable on these points:

- **Defaults.** The library uses factory values read from two never-modified
  units through the OEM wired controller (2026-11-03), not the doc's Default
  column. They differ for K1–K7, AP28, AP29, AP34, AP35, AP39, AP41, AP42 and
  AP46; see the comment above `s_advanced[]`.
- **AP39 (`reg2039`) is ambiguous.** The doc calls it "Whether reduce the
  working frequency or not when reach the setting temperature", which reads as
  a yes/no setting. Yet it gives a 10–120 min range and says "40 is not
  reducing frequency, and other value is reducing frequency", which makes 40
  alone a sentinel and leaves unclear what the minutes count. The doc's default
  is 40 (off), but the factory value is 50 (on, by that rule). We expose it as
  the doc describes (10–120 min) and haven't confirmed any of this against the
  unit's behaviour.

## Unverified registers

This is the to-do list for the map: every byte in either wire window that isn't
verified against something independent, such as the OEM controller's A-code
screen, a tachometer, a meter or a bench write. When a byte is confirmed, move
it into the tables above with its evidence and delete its row here.

**Wire offset** is the real `addr` byte offset on the bus. The `reg` numbers are
library labels (see [Register numbering](#register-numbering)): telemetry
offset = reg − 2093, holding offset = 50 + (reg − 2000).

**Evidence base:** the `arctic_logger` captures from both Sun Peaks units
(HP1 ≈ 231k rows, HP2 ≈ 214k rows; July to 24 Sep 2026, mostly cooling).
"Constant" means constant over that period only. Winter heating may move some
of these bytes.

Status: ❌ current mapping looks wrong · ❓ live, with a hypothesis ·
🔍 live, meaning unknown · 🔒 constant, probably a setting or unused.

Every code on the OEM controller's display list (Arctic manual: o1–o4, o6,
A1–A14; o5 is "not used") is now mapped to a byte in the tables above. The
bytes below are **not** shown on the OEM display at all.

### Disputed and candidate mappings

| reg | wire offset | status | observed | hypothesis | how to verify |
|-----|-------------|--------|----------|------------|---------------|
| 2002 | 52 | ❓ | 0 when stopped; 5–18 while running, about ⅔ of reg2000 (AC current) | Compressor phase current or DC current, A | **Bench 2026-11-03:** 2002 = 13 didn't appear on any A- or o-code the OEM shows, so it's internal-only. Check against a clamp meter on the compressor leads |
| 2003 | 53 | ❓ (scale is verified) | Running speed is always 690–720 RPM; never higher | Is 700 RPM the maximum or a middle speed? The controller's `MACON_FAN_SPEED_MAX_RPM` = 1000 is a placeholder | Watch for faster speeds in deep cold; check the OEM manual or a fan speed parameter |

### Live, meaning unknown

| reg | wire offset | status | observed | hypothesis | how to verify |
|-----|-------------|--------|----------|------------|---------------|
| 2107 | 14 | ❓ | One-hot 16 / 32 / 64 / 128 (rarely 0). Steps at fixed local times, about 06:18, 09:18, 17:18 and 23:18, on both units | Time-of-day schedule or tariff period | Compare with the OEM timer/schedule settings; simulator value test |

### Constant bytes

The values are identical on HP1 and HP2. Many look like configuration in
°C, minutes or steps. Bytes marked "signed?" read as negative if the byte is
treated as signed.

| reg | wire offset | value | reg | wire offset | value |
|-----|-------------|-------|-----|-------------|-------|
| 2097 | 4 | 1 (HP1 showed 5 twice) | 2004–2006 | 54–56 | 0 |
| 2098 | 5 | 0 | 2009 | 59 | 35 (2010 and 2011 next to it are the mode 3/4 setpoints, both 45) |
| 2099 | 6 | 15 | | | |
| 2100 | 7 | 30 | | | |
| 2101 | 8 | 23 (not A13: a bench write of 19 didn't move OEM A13) | | | |
| 2102 | 9 | 6 | 2020 | 70 | 4 |
| 2103 | 10 | 9 | 2021 | 71 | 45 |
| 2104 | 11 | 17 (not A5: bench writes of 21 and 33 didn't move OEM A5) | | | |
| 2105 | 12 | 35 | 2022 | 72 | 0 |
| 2106 | 13 | 0 | 2028 | 78 | 0 |
| 2108 | 15 | 35 | 2036 | 86 | 95 |
| 2109 | 16 | 30 | 2037 | 87 | 15 |
| 2110 | 17 | 40 | 2050 | 100 | 0 |
| 2111–2112 | 18–19 | 0 | 2051 | 101 | 20 |
| 2115 | 22 | 1 | 2052 | 102 | 241 (signed? −15) |
| 2116–2123 | 23–30 | 0 | 2053–2056 | 103–106 | 0 |
| 2124 | 31 | 96 | 2057 | 107 | 8 |
| 2131 | 38 | 0 | | | |
| 2139 | 46 | 0 | | | |

Holding bytes that `macon_advanced_params.cpp` maps to an installer parameter
(AP13–AP47) are left out of this list. Those parameters are live settings,
so their values are expected to stay fixed.

## Named fields (semantic API)

Consumers should not hard-code the tables above. `macon_fields.h` exposes each
field by a stable name (e.g. `outlet_water_temp`, `fan_speed`, `fan_on`,
`working_mode`, `operating_direction`) with its unit, range and step;
`macon_field_set()` rejects out-of-range or off-step values in strict mode and
leaves the image untouched. Each field's `oem_code` is its code on the OEM
display list (`"A13"`, `"o6"`, …) or `nullptr`, so a UI can label readings
"A13 - AC Voltage" to match Arctic's manual. Faults are addressed by code (every site of the
code) or by a single physical site (`macon_fault_sites_for_id`,
`MaconImage::set_fault_site`) — E28 and E05 each have two sites. The RUN bit
(reg2007 bit5) is informational and is not a fault site.

`macon_layout_fingerprint()` hashes the observable field→wire mapping and
`macon_catalog_fingerprint()` hashes names, ranges and the fault catalog; a
simulator and controller that report equal fingerprints and
`MACON_SEMANTIC_API_VERSION` agree on the whole map. `tests/test_conformance.cpp`
pins the library to real OEM capture bytes so that agreement can't hide a
shared mistake.

## Fault registers

The five 8-bit fault bitfields (`reg2007`, `reg2125`, `reg2126`, `reg2127`,
`reg2128`) are decoded bit-by-bit in `macon_faults.{h,cpp}`. `reg2007` bit5
(`0x20`) is the hot-water RUN indicator (INFO, not a fault).
