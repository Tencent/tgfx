// AlphaThresholdShader fragment shader
// Permutation dimensions: GP_TYPE, HAS_XP
// Applies step(threshold, alpha) to discard pixels below threshold.
#version 450

#ifndef GP_TYPE
#define GP_TYPE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#if GP_TYPE == 1
layout(location = 0) in float vCoverage;
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  float Threshold;
#include "xp_uniforms.inc"
};

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 inputColor = Color;
  vec4 result = vec4(0.0);
  if (inputColor.a > 0.0) {
    result.rgb = inputColor.rgb / inputColor.a;
    result.a = step(Threshold, inputColor.a);
    result = clamp(result, 0.0, 1.0);
  }
#if GP_TYPE == 1
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
