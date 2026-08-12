// TextureGradientShader fragment shader
// Processor layout: DefaultGeometryProcessor() + ClampedGradientEffect() + EmptyXferProcessor/PorterDuffXP
// Colorizer: TextureGradientColorizer (16+ stop gradient, texture lookup)
// The gradient layout is selected at runtime through the LayoutType uniform.
// Permutation dimensions (injected by build tool as #define):
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
// Runtime uniforms:
//   LayoutType (int): 0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1
#ifndef HAS_DEVICE_MASK
#define HAS_DEVICE_MASK 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 leftBorderColor;
  vec4 rightBorderColor;
  int LayoutType;
  float Bias;
  float Scale;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(set = 1, binding = 0) uniform sampler2D GradientTexture;

layout(location = 0) in vec2 TransformedCoords_0;

#if HAS_DEVICE_MASK
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 2
#else
  #define XP_DST_TEX_BINDING 1
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "gradient_layout.inc"

layout(location = 0) out vec4 fragColor;

vec4 colorize(float t) {
  return texture(GradientTexture, vec2(t, 0.5));
}

void main() {
  vec4 outputColor = Color;
  highp vec2 coord = TransformedCoords_0;

  float t = gradientLayoutT(LayoutType, Bias, Scale, coord);

  vec4 gradColor;
  if (t <= 0.0) {
    gradColor = leftBorderColor;
  } else if (t >= 1.0) {
    gradColor = rightBorderColor;
  } else {
    gradColor = colorize(t);
  }

  gradColor.rgb *= gradColor.a;
  gradColor *= outputColor.a;

#define TGFX_COVERAGE_SRC_COLOR gradColor
#include "coverage_output.inc"
#include "xp_output.inc"
}
