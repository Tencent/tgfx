// LumaShader fragment shader
// Permutation dimensions: HAS_XP
// Computes luminance from input color using configurable coefficients.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
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

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 inputColor = Color;
  // Unpremultiply to get linear RGB
  vec3 rgb = (inputColor.a > 0.0) ? inputColor.rgb / inputColor.a : vec3(0.0);
  // Compute luminance using BT.709 or custom coefficients
  float luma = dot(rgb, vec3(Kr, Kg, Kb));
  // Output premultiplied grayscale
  vec4 result = vec4(luma * inputColor.a, luma * inputColor.a, luma * inputColor.a, inputColor.a);
#define TGFX_XP_SRC_COLOR result
#include "xp_output.inc"
}
