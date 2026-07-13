// AlphaThresholdShader fragment shader
// Permutation dimensions: HAS_XP
// Applies step(threshold, alpha) to discard pixels below threshold.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  float Threshold;
#include "xp_uniforms.inc"
};

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 inputColor = Color;
  float alpha = inputColor.a;
  float mask = step(Threshold, alpha);
#define TGFX_XP_SRC_COLOR (inputColor * mask)
#include "xp_output.inc"
}
