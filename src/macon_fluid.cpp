#include "macon_fluid.h"

namespace arctic {

namespace {

struct FluidPoint {
    float cp;       // J/(kg*K)
    float density;  // kg/L
};

constexpr int kConcSteps = 7;   // 0, 10 .. 60 % by volume
constexpr int kTempSteps = 7;   // 0, 10 .. 60 degC
constexpr float kStep    = 10.0f;

// APG: rows = 0,10..60 % glycol by volume; columns = 0,10..60 degC.
static const FluidPoint kPropylene[kConcSteps][kTempSteps] = {
    {{4219, 0.9999f}, {4195, 0.9997f}, {4184, 0.9983f}, {4180, 0.9957f}, {4179, 0.9923f}, {4181, 0.9881f}, {4185, 0.9832f}},
    {{4042, 1.0139f}, {4058, 1.0112f}, {4075, 1.0081f}, {4091, 1.0045f}, {4107, 1.0005f}, {4123, 0.9959f}, {4139, 0.9908f}},
    {{3929, 1.0258f}, {3951, 1.0227f}, {3973, 1.0190f}, {3994, 1.0148f}, {4016, 1.0102f}, {4038, 1.0050f}, {4060, 0.9993f}},
    {{3793, 1.0362f}, {3820, 1.0325f}, {3848, 1.0283f}, {3875, 1.0236f}, {3903, 1.0184f}, {3930, 1.0127f}, {3958, 1.0064f}},
    {{3636, 1.0451f}, {3669, 1.0409f}, {3702, 1.0362f}, {3735, 1.0310f}, {3768, 1.0253f}, {3801, 1.0191f}, {3834, 1.0123f}},
    {{3455, 1.0527f}, {3493, 1.0480f}, {3532, 1.0429f}, {3571, 1.0372f}, {3609, 1.0310f}, {3648, 1.0243f}, {3686, 1.0170f}},
    {{3250, 1.0590f}, {3295, 1.0539f}, {3339, 1.0482f}, {3383, 1.0421f}, {3427, 1.0355f}, {3471, 1.0283f}, {3515, 1.0207f}},
};
// AEG: rows = 0,10..60 % glycol by volume; columns = 0,10..60 degC.
static const FluidPoint kEthylene[kConcSteps][kTempSteps] = {
    {{4219, 0.9999f}, {4195, 0.9997f}, {4184, 0.9983f}, {4180, 0.9957f}, {4179, 0.9923f}, {4181, 0.9881f}, {4185, 0.9832f}},
    {{3937, 1.0187f}, {3954, 1.0163f}, {3972, 1.0133f}, {3989, 1.0099f}, {4007, 1.0060f}, {4024, 1.0016f}, {4042, 0.9967f}},
    {{3769, 1.0357f}, {3792, 1.0329f}, {3815, 1.0297f}, {3838, 1.0260f}, {3861, 1.0218f}, {3884, 1.0172f}, {3907, 1.0120f}},
    {{3589, 1.0518f}, {3617, 1.0488f}, {3645, 1.0452f}, {3674, 1.0413f}, {3702, 1.0368f}, {3730, 1.0318f}, {3759, 1.0264f}},
    {{3401, 1.0668f}, {3435, 1.0635f}, {3468, 1.0597f}, {3502, 1.0554f}, {3535, 1.0506f}, {3569, 1.0454f}, {3602, 1.0396f}},
    {{3203, 1.0811f}, {3242, 1.0775f}, {3281, 1.0733f}, {3319, 1.0687f}, {3358, 1.0637f}, {3396, 1.0581f}, {3435, 1.0520f}},
    {{2997, 1.0946f}, {3040, 1.0907f}, {3084, 1.0863f}, {3127, 1.0814f}, {3171, 1.0760f}, {3215, 1.0701f}, {3258, 1.0637f}},
};

float clampf(float v, float lo, float hi) {
    if (!(v == v)) return lo;  // NaN
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

FluidProperties loop_fluid_properties(LoopFluid fluid, float glycol_pct_by_volume,
                                      float mean_temp_c) {
    const FluidPoint (*table)[kTempSteps] =
        fluid == LoopFluid::EthyleneGlycol ? kEthylene : kPropylene;
    const float pct = fluid == LoopFluid::Water
        ? 0.0f
        : clampf(glycol_pct_by_volume, static_cast<float>(kGlycolPctMin),
                 static_cast<float>(kGlycolPctMax));
    const float temp = clampf(mean_temp_c, 0.0f, kStep * (kTempSteps - 1));

    // Bilinear interpolation between the four surrounding table points.
    int ci = static_cast<int>(pct / kStep);
    int ti = static_cast<int>(temp / kStep);
    if (ci > kConcSteps - 2) ci = kConcSteps - 2;
    if (ti > kTempSteps - 2) ti = kTempSteps - 2;
    const float cf = pct / kStep - static_cast<float>(ci);
    const float tf = temp / kStep - static_cast<float>(ti);

    const FluidPoint &a = table[ci][ti];
    const FluidPoint &b = table[ci][ti + 1];
    const FluidPoint &c = table[ci + 1][ti];
    const FluidPoint &d = table[ci + 1][ti + 1];
    auto lerp = [](float x, float y, float f) { return x + (y - x) * f; };

    FluidProperties p;
    p.cp_j_per_kgK = lerp(lerp(a.cp, b.cp, tf), lerp(c.cp, d.cp, tf), cf);
    p.density_kg_per_l =
        lerp(lerp(a.density, b.density, tf), lerp(c.density, d.density, tf), cf);
    return p;
}

PerformanceInputs loop_performance_inputs(float flow_lpm, LoopFluid fluid,
                                          float glycol_pct_by_volume, float mean_temp_c) {
    const FluidProperties p = loop_fluid_properties(fluid, glycol_pct_by_volume, mean_temp_c);
    return PerformanceInputs{flow_lpm, p.cp_j_per_kgK, p.density_kg_per_l};
}

}  // namespace arctic