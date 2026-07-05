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

All code is in namespace `arctic` (registers/faults) and `tuya_codec` (codec).

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
`#include "macon_faults.h"`.

## Fault table

The five fault registers are all 8-bit bitfields. `reg 2007` bit 5 (`0x20`) is
the hot-water **RUN** indicator, not a fault (severity `INFO`, skipped by
`macon_decode_faults`). The rest were reverse-engineered live on the real unit
(2026-07-05) one bit at a time against the OEM LCD and the Smart Life app.
See `src/macon_faults.cpp` for the full annotated table.
