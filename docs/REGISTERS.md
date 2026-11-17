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

### Register addressing

A register address **is** its wire byte offset into the unified page, written
`@N` (decimal) in this doc, the code and logs. The unit answers two read
windows:

| wire window (`addr`,`count`) | covers |
|------------------------------|--------|
| `0`, `50`  (telemetry) | `@0 … @49`  — byte *k* = `@k` |
| `50`, `58` (holding)   | `@50 … @107` — byte *k* = `@(50+k)` |

A single-register fc=0x06 write to `@N` puts `addr = N` on the wire. Offsets
outside both windows (e.g. the AP48–AP51 bytes at `@110 … @114`) are not
readable, and `MaconLink::write_register()` refuses them.

**Historical note.** Before semantic API version 2 the library used synthetic
labels borrowed from Arctic's old Modbus document. Old notes, logs and issues
may still use them:

| old label | offset |
|-----------|--------|
| `reg2093 … reg2142` (telemetry) | `@(reg − 2093)` → `@0 … @49` |
| `reg2000 … reg2092` (holding / AP block) | `@(reg − 1950)` → `@50 … @142` |

## Setpoints (fc=0x06 writes)  ← the important part

The OEM controller (bus master) SETS a setpoint by writing **one signed byte**
(whole °C) to the setpoint's wire byte offset with fc=0x06, and the mainboard
replies with a 9-byte ACK echoing `addr`+`count` (no data). Confirmed live
2026-07-09.

| function | wire addr | live value seen | status |
|----------|-----------|-----------------|--------|
| **Cooling setpoint** | `0x0000` | 12 → 24 (dial), −5 min | **confirmed** |
| **Heating setpoint** (modes 1 and 2) | `0x0001` | 40 | Confirmed on the bench 2026-09-26 (OEM wired controller wrote 47 here) |
| **Hot-water setpoint** (live target) | `0x0002` | 50 → 38 (dial) | **confirmed** |

Example (hot-water 50 → 38 °C):

```
controller → unit : 55 AA F0 06 0002 0001 26 <chk>   (write @2 = 0x26 = 38)
unit → controller : 55 AA 0F 06 0002 0001 <chk>       (ACK, echoes addr+count, no data)
```

A non-exception ACK means **accepted + applied** (Modbus semantics), so the ACK
is the authoritative confirmation. A NACK/exception frame (`fc | 0x80`) has
**never been observed** on this unit — its exact shape is unconfirmed (tracked
as the `macon-nack-probe` follow-up). `MaconLink` detects a `0x86` best-effort.

### Not a live setpoint: the hot-water *ceiling*

| function | offset | note |
|----------|--------|------|
| **Hot-water ceiling** | `@62` (Cn13) | The manual's Cn13 = "Highest setting temperature of hot water, heating mode" (range 20~55 °C, default 50). This is the mainboard's enforced **ceiling**, NOT the live user setpoint. Dialing the display setpoint down did not move @62 (stayed at 50). |

`MaconLink` (`macon_link.{h,cpp}`) exposes the intent-named transactions:
`set_cooling_setpoint()` / `set_hot_water_setpoint()` / `set_working_mode()` (write + ACK) and
`read_cooling_setpoint()` / `read_hot_water_setpoint()` / `read_heating_setpoint()`
(@1, untested).

## Telemetry window (`@0 … @49`)

| offset | code | field | scale / notes |
|-----|------|-------|---------------|
| @0 | — | Cooling setpoint | signed °C (wire 0x0000) |
| @1 | — | Heating setpoint | signed °C (wire 0x0001, writable; used by modes 1 and 2) |
| @2 | — | Hot-water setpoint | signed °C (wire 0x0002) |
| @3 | — | Selected working mode | 0 cooling, 1 heating, 5 hot water, 6 hot water / cooling (wire 0x0003, writable). 2/3/4 are heating variants, decoded but not writable: 2 = flashing heating icon with the @1 setpoint, decoded as "Freeze protection" (`freeze_protection`) because the field units switch to it while idle whenever inlet/outlet water reaches 2.0 °C, run the pump ~30 s, then return to the selected mode; 3 =  solid icon with holding @60, 4 = flashing icon with holding @61. 7+ are out of range. |
| @20 | A8  | IPM temp | signed °C |
| @21 | A9  | Real-time power | ×100 = W |
| @32 | — | Fault: sensor/EE/comm E-codes | bitfield |
| @33 | — | Fault: sensor/comm/compressor | bitfield |
| @34 | — | Fault: electrical/power-stage | bitfield |
| @35 | — | Fault: refrigerant/protection (P-codes) | bitfield |
| @36 | — | Icon bits #2 | bit1 defrost, bit4 fan |
| @37 | — | Status | bit2 compressor *icon* (not "running" — use frequency > 0), bit3 pump |
| @39 | o3  | Outlet (supply) water temp | signed °C |
| @40 | o2  | Inlet (return) water temp | signed °C |
| @41 | o4  | Outdoor ambient temp | signed °C |
| @42 | A6  | Cool coil temp | signed °C |
| @43 | A2  | Coil temp | signed °C |
| @44 | A3  | Suction temp | signed °C |
| @45 | A1  | Discharge temp | signed °C |
| @47 | A5  | Main EEV opening (`primary_eev`) | Raw byte, shown as-is by the OEM (bench-verified 2026-11-03: wrote 37, OEM A5 = 37). 48 in cooling, 12–20 regulating in heating. **ASSUMPTION:** ×10 = steps, full travel 500 steps (AP50 is 0–500); the library reports `primary_eev` in steps (raw × 10). See [EEV opening unit](#eev-opening-unit). Before 2026-11 the library read A5 from @11, a constant setting byte. |
| @48 | A14 | Compressor frequency | Hz |
| @49 | A13 | AC input voltage (`ac_voltage`) | V ×1 (live 217–245). The OEM shows V ÷ 10, truncated (bench-verified 2026-11-03: 211 → A13 21, 187 → A13 18). Before 2026-11 the library read A13 from @8 ×10, a constant setting byte that always gave 230 V. |

## Holding window (`@50 … @107`)

| offset | code | field | scale / notes |
|-----|------|-------|---------------|
| @50 | A4  | AC input current | A |
| @51 | A7  | DC bus voltage | ×10 = V |
| @53 | A10 | DC (fan) motor speed | ×10 = RPM (tach-verified: raw 70 = 700 RPM) |
| @57 | — | Run-state / fault code | `0x00` off, `0x20` hot-water RUN, low/high = P-faults |
| @58 | o1  | Water tank temp | signed °C (real immersed DHW probe) |
| @62 | Cn13 | Hot-water ceiling | see setpoints section |
| @63 | A11 | High pressure (`high_pressure`) | Signed byte, shown as-is (bench-verified 2026-11-03: wrote 9, OEM A11 = 9; the live value 250 shows as −6). Unit and scale unknown. Constant −6 on both units, and every OEM log sent to Arctic shows A11 = −6, so no transducer is fitted. The library decodes it for units that have one; a UI should hide it by default. |
| @64 | A12 | Low pressure (`low_pressure`) | Raw byte, shown as-is (bench-verified 2026-11-03: wrote 7, OEM A12 = 7). Unit and scale unknown. Constant 3 on both units and in every OEM log sent to Arctic, so no transducer is fitted. Decoded for units that have one; hide by default. |
| @98 | o6  | Auxiliary (EVI) EEV opening (`aux_eev`) | Raw byte, shown as-is (bench-verified 2026-11-03: wrote 29, OEM o6 = 29; Arctic docs: "Auxiliary elec. expansion valve degree"). 0 almost always; opens only in some heating/idle conditions. **ASSUMPTION:** ×10 = steps (same as A5); reported in steps. See [EEV opening unit](#eev-opening-unit). |
| @99 | — | Operating direction | `0x00` heating, `0x04` cooling (reversing valve) |

The installer **Cn** advanced-parameter block (mostly `@63 … @107`, plus `@18`/`@19` and the AP48–AP51 bytes
beyond the read windows up to `@114`; the AP→offset map is a table, not a
formula) is documented and guarded separately in
`macon_advanced_params.{h,cpp}` (validate-only, reject-not-clamp; no write path).

### EEV opening unit

`primary_eev` (A5) and `aux_eev` (o6) are reported in motor **steps**, as the
wire byte **× 10**, with a full travel of **500 steps**
(`MACON_EEV_MAX_STEPS`, the catalog `max` of both fields). **Both the ×10
scale and the 500-step travel are ASSUMPTIONS** pending the bench check below.

Why: Arctic's docs call them the "main / auxiliary elec. expansion valve
degree"; "degree" is a translation of 开度 ("opening degree"), meaning how far
the valve is open, not an angle. Stepper-driven EEVs are normally counted in
motor steps (pulses) with ~480–500 for full travel, which fits the 0–500 range
of the manual-opening parameters AP50/AP51 (`@112`/`@114`). The wire byte
tops out at 255, though, and the units read 12–20 heating and 48 cooling, so
the OEM display (which shows the byte as-is) is most likely steps ÷ 10.
Consumers show steps first, with a percentage of `MACON_EEV_MAX_STEPS` as a
secondary figure.

The byte is **signed**. After every compressor stop the unit re-zeros both
valves: it opens them to ~48, drives them past fully closed (the count runs
down to about −30, i.e. 226 on the wire), reopens to ~33–39 and settles at
~15 for standby, all within about a minute (seen on both Sun Peaks units,
2026-11-04). The stepper has no position sensor, so overdriving against the
closed stop is how the controller re-syncs its step count. The library
reports any negative reading as 0 (closed).

To settle it: in manual EEV mode (AP48), set AP50 to a known opening (e.g. 300) and
see whether A5 shows 300, 30 or something else. If A5 shows 30, the assumption
holds; otherwise change the field step/max and the decode in `macon_state.cpp`.

### Advanced-parameter quirks

Arctic's "Appendix C - Advanced parameter setting V4 (Cn31)" is the source for
the AP table, but it isn't reliable on these points:

- **Defaults.** The library uses factory values read from two never-modified
  units through the OEM wired controller (2026-11-03), not the doc's Default
  column. They differ for K1–K7, AP28, AP29, AP34, AP35, AP39, AP41, AP42 and
  AP46; see the comment above `s_advanced[]`.
- **AP39 (`@89`) is ambiguous.** The doc calls it "Whether reduce the
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

Each byte is identified by its wire offset (`@N`, the real `addr` byte offset
on the bus; see [Register addressing](#register-addressing)).

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

| wire offset | status | observed | hypothesis | how to verify |
|-------------|--------|----------|------------|---------------|
| @52 | ❓ | 0 when stopped; 5–18 while running, about ⅔ of @50 (AC current) | Compressor phase current or DC current, A | **Bench 2026-11-03:** @52 = 13 didn't appear on any A- or o-code the OEM shows, so it's internal-only. Check against a clamp meter on the compressor leads |
| @53 | ❓ (scale is verified) | Running speed is always 690–720 RPM; never higher | Is 700 RPM the maximum or a middle speed? The controller's `MACON_FAN_SPEED_MAX_RPM` = 1000 is a placeholder | Watch for faster speeds in deep cold; check the OEM manual or a fan speed parameter |

### Live, meaning unknown

| wire offset | status | observed | hypothesis | how to verify |
|-------------|--------|----------|------------|---------------|
| @14 | ❓ | One-hot 16 / 32 / 64 / 128 (rarely 0). Steps at fixed local times, about 06:18, 09:18, 17:18 and 23:18, on both units | Time-of-day schedule or tariff period | Compare with the OEM timer/schedule settings; simulator value test |

### Constant bytes

The values are identical on HP1 and HP2. Many look like configuration in
°C, minutes or steps. Bytes marked "signed?" read as negative if the byte is
treated as signed.

| offset | value | offset | value |
|--------|-------|--------|-------|
| @4 | 1 (HP1 showed 5 twice) | @54–@56 | 0 |
| @5 | 0 | @59 | 35 (@60 and @61 next to it are the mode 3/4 setpoints, both 45) |
| @6 | 15 | | |
| @7 | 30 | | |
| @8 | 23 (not A13: a bench write of 19 didn't move OEM A13) | | |
| @9 | 6 | @70 | 4 |
| @10 | 9 | @71 | 45 |
| @11 | 17 (not A5: bench writes of 21 and 33 didn't move OEM A5) | | |
| @12 | 35 | @72 | 0 |
| @13 | 0 | @78 | 0 |
| @15 | 35 | @86 | 95 |
| @16 | 30 | @87 | 15 |
| @17 | 40 | @100 | 0 |
| @18–@19 | 0 | @101 | 20 |
| @22 | 1 | @102 | 241 (signed? −15) |
| @23–@30 | 0 | @103–@106 | 0 |
| @31 | 96 | @107 | 8 |
| @38 | 0 | | |
| @46 | 0 | | |

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
(@57 bit5) is informational and is not a fault site.

`macon_layout_fingerprint()` hashes the observable field→wire mapping and
`macon_catalog_fingerprint()` hashes names, ranges and the fault catalog; a
simulator and controller that report equal fingerprints and
`MACON_SEMANTIC_API_VERSION` agree on the whole map. `tests/test_conformance.cpp`
pins the library to real OEM capture bytes so that agreement can't hide a
shared mistake.

## Fault registers

The five 8-bit fault bitfields (`@57`, `@32`, `@33`, `@34`,
`@35`) are decoded bit-by-bit in `macon_faults.{h,cpp}`. `@57` bit5
(`0x20`) is the hot-water RUN indicator (INFO, not a fault).
