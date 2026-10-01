// Native unit test for loop-fluid properties (macon_fluid.h).

#include "macon_fluid.h"

#include <cmath>
#include <cstdio>

using namespace arctic;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main() {
    // Water at 20 degC: ASHRAE / IAPWS values.
    FluidProperties w = loop_fluid_properties(LoopFluid::Water, 0.0f, 20.0f);
    CHECK(near(w.cp_j_per_kgK, 4184.0f, 1.0f));
    CHECK(near(w.density_kg_per_l, 0.9983f, 0.0005f));

    // Water ignores the glycol percentage.
    FluidProperties w30 = loop_fluid_properties(LoopFluid::Water, 30.0f, 20.0f);
    CHECK(w30.cp_j_per_kgK == w.cp_j_per_kgK);

    // Table points are reproduced exactly.
    FluidProperties pg30 = loop_fluid_properties(LoopFluid::PropyleneGlycol, 30.0f, 20.0f);
    CHECK(near(pg30.cp_j_per_kgK, 3848.0f, 0.5f));
    CHECK(near(pg30.density_kg_per_l, 1.0283f, 0.0001f));
    FluidProperties eg30 = loop_fluid_properties(LoopFluid::EthyleneGlycol, 30.0f, 20.0f);
    CHECK(near(eg30.cp_j_per_kgK, 3645.0f, 0.5f));
    CHECK(near(eg30.density_kg_per_l, 1.0452f, 0.0001f));

    // Bilinear midpoint: 35% at 25 degC averages the four corners.
    FluidProperties mid = loop_fluid_properties(LoopFluid::PropyleneGlycol, 35.0f, 25.0f);
    CHECK(near(mid.cp_j_per_kgK, (3848.0f + 3875.0f + 3702.0f + 3735.0f) / 4.0f, 0.5f));

    // More glycol -> lower cp, higher density.
    FluidProperties pg10 = loop_fluid_properties(LoopFluid::PropyleneGlycol, 10.0f, 20.0f);
    FluidProperties pg50 = loop_fluid_properties(LoopFluid::PropyleneGlycol, 50.0f, 20.0f);
    CHECK(pg10.cp_j_per_kgK > pg30.cp_j_per_kgK && pg30.cp_j_per_kgK > pg50.cp_j_per_kgK);
    CHECK(pg10.density_kg_per_l < pg30.density_kg_per_l);
    // Ethylene glycol has a lower cp than propylene at the same concentration.
    CHECK(eg30.cp_j_per_kgK < pg30.cp_j_per_kgK);

    // Out-of-range inputs clamp instead of extrapolating.
    FluidProperties hi = loop_fluid_properties(LoopFluid::PropyleneGlycol, 90.0f, 99.0f);
    FluidProperties top = loop_fluid_properties(LoopFluid::PropyleneGlycol, 60.0f, 60.0f);
    CHECK(hi.cp_j_per_kgK == top.cp_j_per_kgK);
    FluidProperties lo = loop_fluid_properties(LoopFluid::PropyleneGlycol, -5.0f, -20.0f);
    FluidProperties water0 = loop_fluid_properties(LoopFluid::Water, 0.0f, 0.0f);
    CHECK(lo.cp_j_per_kgK == water0.cp_j_per_kgK);
    FluidProperties nan = loop_fluid_properties(LoopFluid::PropyleneGlycol, NAN, NAN);
    CHECK(nan.cp_j_per_kgK == water0.cp_j_per_kgK);

    // Volumetric heat capacity of 30% PG is ~5% below water.
    PerformanceInputs in = loop_performance_inputs(40.0f, LoopFluid::PropyleneGlycol, 30.0f, 20.0f);
    CHECK(in.water_flow_lpm == 40.0f);
    const float ratio = (in.fluid_cp_j_per_kgK * in.fluid_density_kg_per_l) /
                        (w.cp_j_per_kgK * w.density_kg_per_l);
    CHECK(ratio > 0.93f && ratio < 0.96f);

    if (g_failures == 0) std::printf("test_fluid: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
