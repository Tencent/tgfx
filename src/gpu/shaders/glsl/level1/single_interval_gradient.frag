// SingleIntervalGradientShader fragment shader
// Processor layout: GP + ClampedGradientEffect() + EmptyXferProcessor/PorterDuffXP
// Colorizer: SingleIntervalGradientColorizer (2-stop gradient, simple mix)
// The gradient layout is selected at runtime through the LayoutType uniform.
// Permutation dimensions (injected by build tool as #define):
//   GP_TYPE: 0=DefaultGP, 1=QuadPerEdgeAAGP
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
// Runtime uniforms:
//   LayoutType (int): 0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND
#version 450

#ifndef GP_TYPE
#define GP_TYPE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
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
  vec4 start;
  vec4 end;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_VCOVERAGE
layout(location = 1) in float vCoverage;
#endif

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
  if (LayoutType == 1) {
    // Radial: t = length
    return length(coord);
  } else if (LayoutType == 2) {
    // Conic: t = angle-based
    float angle = atan(-coord.y, -coord.x);
    return ((angle * 0.15915494309180001 + 0.5) + Bias) * Scale;
  } else if (LayoutType == 3) {
    // Diamond: t = max(|x|, |y|)
    return max(abs(coord.x), abs(coord.y));
  }
  // Linear (LayoutType == 0): t = x
  return coord.x + 1.0000000000000001e-05;
}

vec4 colorize(float t) {
  return mix(start, end, t);
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

#if HAS_VCOVERAGE
#define TGFX_COVERAGE_SRC_COLOR (gradColor * vCoverage)
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_COVERAGE_SRC_COLOR gradColor
#endif
#include "coverage_output.inc"
#include "xp_output.inc"
}
