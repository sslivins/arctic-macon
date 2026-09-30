#pragma once

// Heat-transfer fluid properties for the performance estimate.
//
// The loop fluid's specific heat and density both change with glycol
// concentration and temperature, and they multiply straight into the heat
// output (Q = flow * density * cp * dT). A 30% propylene glycol loop carries
// about 8% less heat per litre per degree than plain water, so using water
// properties for a glycol loop overstates heat output and COP.
//
// Values come from the ASHRAE secondary-coolant tables (via CoolProp's APG/AEG
// fits), tabulated every 10% by volume and every 10 °C and interpolated
// linearly in between.

#include <stdint.h>

#include "macon_state.h"

namespace arctic {

enum class LoopFluid : uint8_t {
    Water           = 0,
    PropyleneGlycol = 1,
    EthyleneGlycol  = 2,
};

// Supported range of the glycol concentration, % by volume.
constexpr int kGlycolPctMin = 0;
constexpr int kGlycolPctMax = 60;

struct FluidProperties {
    float cp_j_per_kgK;
    float density_kg_per_l;
};

/// Specific heat and density of the loop fluid at `mean_temp_c`.
///
/// `glycol_pct_by_volume` is ignored for Water and clamped to 0..60 otherwise;
/// the temperature is clamped to 0..60 °C (the tables' range).
FluidProperties loop_fluid_properties(LoopFluid fluid, float glycol_pct_by_volume,
                                      float mean_temp_c);

/// Performance inputs for a volumetric flow of the given fluid.
PerformanceInputs loop_performance_inputs(float flow_lpm, LoopFluid fluid,
                                          float glycol_pct_by_volume, float mean_temp_c);

}  // namespace arctic
