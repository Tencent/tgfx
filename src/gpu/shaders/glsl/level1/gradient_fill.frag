// GradientFillShader fragment shader
// Processor layout: DefaultGeometryProcessor() + ClampedGradientEffect() + EmptyXferProcessor/PorterDuffXP
// The gradient layout (linear/radial/conic/diamond) is selected at runtime through the LayoutType
// uniform instead of a compile-time dimension, so a single variant covers all four layouts.
// Permutation dimensions (injected by build tool as #define):
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend), 2=PorterDuff FBF
// Runtime uniforms:
//   LayoutType (int): 0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND
//   IntervalCount (int, 1~8)
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1
#ifndef HAS_DEVICE_MASK
#define HAS_DEVICE_MASK 0
#endif
#ifndef HAS_VCOVERAGE
#define HAS_VCOVERAGE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 leftBorderColor;
  vec4 rightBorderColor;
  int LayoutType;
  float Bias;
  float Scale;
  int IntervalCount;
  vec4 thresholds1_7;
  vec4 thresholds9_13;
  vec4 scale0_1;
  vec4 scale2_3;
  vec4 scale4_5;
  vec4 scale6_7;
  vec4 scale8_9;
  vec4 scale10_11;
  vec4 scale12_13;
  vec4 scale14_15;
  vec4 bias0_1;
  vec4 bias2_3;
  vec4 bias4_5;
  vec4 bias6_7;
  vec4 bias8_9;
  vec4 bias10_11;
  vec4 bias12_13;
  vec4 bias14_15;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_VCOVERAGE
layout(location = 1) in float vCoverage;
#endif

#if HAS_DEVICE_MASK
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 1
#else
  #define XP_DST_TEX_BINDING 0
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "gradient_layout.inc"
#include "gradient_colorize.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor = Color;
  highp vec2 coord = TransformedCoords_0;

  // Layout: compute t
  float t = gradientLayoutT(LayoutType, Bias, Scale, coord);

  // ClampedGradientEffect: border clamping + colorization
  vec4 gradColor;
  if (t <= 0.0) {
    gradColor = leftBorderColor;
  } else if (t >= 1.0) {
    gradColor = rightBorderColor;
  } else {
    gradColor = gradientColorizeUnrolled(t, IntervalCount, thresholds1_7, thresholds9_13,
                                         scale0_1, bias0_1, scale2_3, bias2_3, scale4_5, bias4_5,
                                         scale6_7, bias6_7, scale8_9, bias8_9, scale10_11,
                                         bias10_11, scale12_13, bias12_13, scale14_15, bias14_15);
  }

  // Premultiply alpha
  gradColor.rgb *= gradColor.a;
  // Multiply by input color alpha (from DefaultGeometryProcessor)
  gradColor *= outputColor.a;

#if HAS_VCOVERAGE
#define TGFX_COVERAGE_SRC_COLOR (gradColor * vCoverage)
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_COVERAGE_SRC_COLOR gradColor
#endif
#include "coverage_output.inc"
#include "xp_output.inc"
}
