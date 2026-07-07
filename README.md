# arctic-macon

Shared library for the **Arctic (Macon) heat pump** wire protocol and register
map. This is the **single source of truth** for the Tuya-MCU codec, the Macon
register table, and the fault-code decoding that were previously copy-pasted
(and drifting) across three repos:

- `arctic-controller` — the Tab5 replacement controller
- `arctic-simulator`  — the heat-pump simulator
- `arctic-sniffer`    — the passive RS-485 bus sniffer

## Contents

| File | What it provides |
|------|------------------|
| `tuya_codec.{h,cpp}` | Frame parse/encode + checksum for the `55 AA …` Tuya-MCU wire format. Pure, dependency-free. |
| `macon_registers.{h,cpp}` | Macon register numbering, per-register metadata (name/unit/scale/signed), value + bitmap formatting, `register_lookup()`. |
| `macon_faults.{h,cpp}` | **Canonical fault table** — the five fault bitfield registers (2007, 2125, 2126, 2127, 2128) mapped bit-by-bit to their LCD/app codes, plus `macon_decode_faults()` returning active `{code, label, severity}`. |
| `macon_advanced_params.{h,cpp}` | The installer-only **advanced ("Cn") parameter** table (`reg = 2000 + Cn`) with per-parameter valid range / default / unit from the vendor doc, plus `validate_advanced_write()` — a **reject-not-clamp** write guardrail. |

All code is in namespace `arctic` (registers/faults/advanced-params) and
`tuya_codec` (codec).

## Advanced ("Cn") parameters — DANGER

`macon_advanced_params.{h,cpp}` describes the installer-only Cn configuration
block (holding registers **2013–2057**, `reg = 2000 + Cn`). These are
**read/write** and dangerous: blind or out-of-range writes have previously put
the controller into a bad/cycling state.

This module deliberately provides **only metadata + a validation guardrail** —
there is **no register-write path** here, so nothing in the library can actually
move a Cn register. `validate_advanced_write(cn, value)` **rejects** (never
clamps) anything out of range / not-in-enum / unknown, and also refuses any
parameter whose exact register index has not yet been confirmed on the
simulator (`needs_sim_confirm`). Only **Cn38** and **Cn40** are index-confirmed
today — both from exact external anchors (Cn40=−1 was the manufacturer's glycol
setting, matching live `reg2040=−1`; Cn38's −30 default matches live
`reg2038=−30`). Clearing `needs_sim_confirm` for the rest is done one parameter
at a time as the simulator pins each index.

## Using it (ESP-IDF)

Add as a submodule under the consuming project's `components/`:

```sh
git submodule add https://github.com/sslivins/arctic-macon.git components/arctic-macon
```

Then list it in the consuming component's `REQUIRES`:

```cmake
idf_component_register(
    SRCS ...
    REQUIRES arctic-macon)
```

and `#include "tuya_codec.h"`, `#include "macon_registers.h"`,
`#include "macon_faults.h"`, `#include "macon_advanced_params.h"`.

## Fault table

The five fault registers are all 8-bit bitfields. `reg 2007` bit 5 (`0x20`) is
the hot-water **RUN** indicator, not a fault (severity `INFO`, skipped by
`macon_decode_faults`). The rest were reverse-engineered live on the real unit
(2026-07-05) one bit at a time against the OEM LCD and the Smart Life app.
See `src/macon_faults.cpp` for the full annotated table.
