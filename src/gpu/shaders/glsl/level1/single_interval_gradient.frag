// SingleIntervalGradientShader fragment shader
// Processor layout: DefaultGeometryProcessor() + ClampedGradientEffect() + EmptyXferProcessor/PorterDuffXP
// Colorizer: SingleIntervalGradientColorizer (2-stop gradient, simple mix)
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

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 leftBorderColor;
  vec4 rightBorderColor;
#if LAYOUT_TYPE == 2
  float Bias;
  float Scale;
#endif
  vec4 start;
  vec4 end;
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"

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

#define TGFX_XP_SRC_COLOR gradColor
#include "xp_output.inc"
}
