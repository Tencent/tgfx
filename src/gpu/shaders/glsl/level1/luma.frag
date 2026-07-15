// LumaShader fragment shader
// Permutation dimensions: GP_TYPE, HAS_XP
// Computes luminance from input color using configurable coefficients.
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
  float Kr;
  float Kg;
  float Kb;
#include "xp_uniforms.inc"
};

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 inputColor = Color;
  float luma = dot(inputColor.rgb, vec3(Kr, Kg, Kb));
  vec4 result = vec4(luma);
#if GP_TYPE == 1
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
