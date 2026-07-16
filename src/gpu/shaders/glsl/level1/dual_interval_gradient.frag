// DualIntervalGradientShader fragment shader
// Processor layout: DefaultGeometryProcessor() + ClampedGradientEffect() + EmptyXferProcessor/PorterDuffXP
// Colorizer: DualIntervalGradientColorizer (3/4-stop gradient, piecewise linear)
// Permutation dimensions (injected by build tool as #define):
//   LAYOUT_TYPE: 0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
#version 450

#ifndef LAYOUT_TYPE
#define LAYOUT_TYPE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 leftBorderColor;
  vec4 rightBorderColor;
#if LAYOUT_TYPE == 2
  float Bias;
  float Scale;
#endif
  vec4 scale01;
  vec4 bias01;
  vec4 scale23;
  vec4 bias23;
  float threshold;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

#if HAS_COVERAGE == 2
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 1
#else
  #define XP_DST_TEX_BINDING 0
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

float computeLayoutT(vec2 coord) {
#if LAYOUT_TYPE == 0
  // Linear: t = x
  return coord.x + 1.0000000000000001e-05;
#elif LAYOUT_TYPE == 1
  // Radial: t = length
  return length(coord);
#elif LAYOUT_TYPE == 2
  // Conic: t = angle-based
  float angle = atan(-coord.y, -coord.x);
  return ((angle * 0.15915494309180001 + 0.5) + Bias) * Scale;
#elif LAYOUT_TYPE == 3
  // Diamond: t = max(|x|, |y|)
  return max(abs(coord.x), abs(coord.y));
#endif
}

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

  float t = computeLayoutT(coord);

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
