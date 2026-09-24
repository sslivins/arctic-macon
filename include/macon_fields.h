#pragma once

// ---------------------------------------------------------------------------
// Named semantic fields — the library-owned vocabulary for reading/writing the
// heat pump state by NAME (e.g. "outlet_water_temp", "fan_on", "working_mode").
//
// Every consumer that exposes the state over an API (the controller's demo
// endpoint, the simulator's REST API) resolves names through this table, so a
// field name, its unit, its accepted range and its wire encoding are defined
// exactly once. Consumers never see a register address.
//
// Also publishes compatibility fingerprints so two firmwares built against the
// library (e.g. controller + simulator on the RS-485 test bench) can verify
// they agree on the wire layout without comparing git SHAs.
//
// Pure, dependency-free C++17.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

#include "macon_image.h"
#include "macon_state.h"

namespace arctic {

// Bumped whenever the named-field / catalog API changes incompatibly.
constexpr uint16_t MACON_SEMANTIC_API_VERSION = 1;

enum class MaconFieldKind : uint8_t {
    Number,              // integer in `unit`, range [min, max], multiple of `step`
    Flag,                // 0 / 1
    WorkingMode,         // user-selected mode; string keys via macon_working_mode_key()
    OperatingDirection,  // live heating/cooling direction; keys via macon_direction_key()
};

struct MaconFieldDesc {
    const char    *name;         // canonical API name
    const char    *aliases[2];   // accepted alternative names (nullptr = none)
    MaconFieldKind kind;
    const char    *unit;         // "C", "Hz", "V", "A", "W", "RPM", "steps"; nullptr if n/a
    int32_t        min;          // Number: accepted semantic range (inclusive)
    int32_t        max;
    int32_t        step;         // Number: value must be a multiple of step
    bool           verified;     // false = register UNVERIFIED on the real unit
    // Binding (library-internal meaning; consumers pass the desc back as-is).
    MaconField     field;
    MaconFlag      flag;
};

extern const MaconFieldDesc MACON_FIELDS[];
extern const size_t         MACON_FIELDS_COUNT;

// Look up a field by canonical name or alias. nullptr if unknown.
const MaconFieldDesc *macon_field_find(const char *name);

// Set a field on the image. For WorkingMode pass (int)MaconWorkingMode; for
// OperatingDirection pass (int)MaconMode. strict=true rejects any value the wire
// cannot represent exactly (OutOfRange, image untouched); strict=false keeps the
// legacy clamp-and-report-Truncated behaviour.
MaconSetResult macon_field_set(MaconImage &img, const MaconFieldDesc &desc,
                               int32_t value, bool strict = true);

// Read a field back from a decoded state (same encoding as macon_field_set).
// Returns false if the field is not valid (register absent) in `s`.
bool macon_field_get(const MaconState &s, const MaconFieldDesc &desc, int32_t *out);

// API string keys for the enum-valued fields.
//   working mode: "cooling", "floor_heating", "fan_coil_heating", "hot_water", "auto"
//   direction:    "heating", "cooling"
const char       *macon_working_mode_key(MaconWorkingMode mode);   // nullptr if Unknown
MaconWorkingMode  macon_working_mode_from_key(const char *key);    // Unknown if bad
const char       *macon_direction_key(MaconMode mode);             // nullptr if Unknown
MaconMode         macon_direction_from_key(const char *key);       // Unknown if bad

// Compatibility fingerprints (FNV-1a 32). Computed by exercising the public
// semantic API against a scratch image, so they change iff observable behaviour
// changes:
//   layout  — wire windows + where/how every named field, flag, working-mode
//             value and fault site lands on the wire.
//   catalog — presentation: field names/units/ranges, fault codes, labels,
//             severities and resolution text.
uint32_t macon_layout_fingerprint();
uint32_t macon_catalog_fingerprint();

}  // namespace arctic
