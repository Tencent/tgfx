// DualIntervalGradientShader fragment shader
// Processor layout: DefaultGeometryProcessor() + ClampedGradientEffect() + EmptyXferProcessor/PorterDuffXP
// Colorizer: DualIntervalGradientColorizer (3/4-stop gradient, piecewise linear)
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
  vec4 scale01;
  vec4 bias01;
  vec4 scale23;
  vec4 bias23;
  float threshold;
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

layout(location = 0) out vec4 fragColor;

vec4 colorize(float t) {
  vec4 scale, bias;
  if (t < threshold) {
    scale = scale01;
    bias = bias01;
  } else {
    scale = scale23;
    bias = bias23;
  }
  return vec4(t * scale + bias);
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

#if HAS_VCOVERAGE
#define TGFX_COVERAGE_SRC_COLOR (gradColor * vCoverage)
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_COVERAGE_SRC_COLOR gradColor
#endif
#include "coverage_output.inc"
#include "xp_output.inc"
}
