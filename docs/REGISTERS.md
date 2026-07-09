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
| **Aux / space-heating setpoint** | `0x0001` | `reg2094` | 40 | **UNVERIFIED** (test unit lacks this stage) |
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
`set_cooling_setpoint()` / `set_hot_water_setpoint()` (write + ACK) and
`read_cooling_setpoint()` / `read_hot_water_setpoint()` / `read_heating_setpoint()`
(reg2094, untested).

## Telemetry window (`reg2093 … reg2142`)

| reg | code | field | scale / notes |
|-----|------|-------|---------------|
| 2093 | — | Cooling setpoint | signed °C (wire 0x0000) |
| 2094 | — | Aux/heating setpoint | signed °C (wire 0x0001, **UNVERIFIED**) |
| 2095 | — | Hot-water setpoint | signed °C (wire 0x0002) |
| 2101 | A13 | AC input voltage | ×10 = V |
| 2104 | A5  | Main EEV | steps |
| 2113 | A8  | IPM temp | signed °C |
| 2114 | A9  | Real-time power | ×100 = W |
| 2125 | — | Fault: sensor/EE/comm E-codes | bitfield |
| 2126 | — | Fault: sensor/comm/compressor | bitfield |
| 2127 | — | Fault: electrical/power-stage | bitfield |
| 2128 | — | Fault: refrigerant/protection (P-codes) | bitfield |
| 2129 | — | Icon bits #2 | bit1 defrost, bit4 fan |
| 2130 | — | Status | bit2 compressor, bit3 pump |
| 2132 | o3  | Outlet (supply) water temp | signed °C |
| 2133 | o2  | Inlet (return) water temp | signed °C |
| 2134 | o4  | Outdoor ambient temp | signed °C |
| 2135 | A6  | Cool coil temp | signed °C |
| 2136 | A2  | Coil temp | signed °C |
| 2137 | A3  | Suction temp | signed °C |
| 2138 | A1  | Discharge temp | signed °C |
| 2141 | A14 | Compressor frequency | Hz |

## Holding window (`reg2000 … reg2057`)

| reg | code | field | scale / notes |
|-----|------|-------|---------------|
| 2000 | A4  | AC input current | A |
| 2001 | A7  | DC bus voltage | ×10 = V |
| 2003 | A10 | DC (fan) motor speed | raw level |
| 2007 | — | Run-state / fault code | `0x00` off, `0x20` hot-water RUN, low/high = P-faults |
| 2008 | o1  | Water tank temp | signed °C (real immersed DHW probe) |
| 2012 | Cn13 | Hot-water ceiling | see setpoints section |
| 2049 | — | Operating direction | `0x00` heating, `0x04` cooling (reversing valve) |

The installer **Cn** advanced-parameter block (`reg = 2000 + Cn`, roughly
`reg2013 … reg2057`) is documented and guarded separately in
`macon_advanced_params.{h,cpp}` (validate-only, reject-not-clamp; no write path).

## Fault registers

The five 8-bit fault bitfields (`reg2007`, `reg2125`, `reg2126`, `reg2127`,
`reg2128`) are decoded bit-by-bit in `macon_faults.{h,cpp}`. `reg2007` bit5
(`0x20`) is the hot-water RUN indicator (INFO, not a fault).
