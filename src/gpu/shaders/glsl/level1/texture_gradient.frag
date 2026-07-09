// TextureGradientShader fragment shader
// Processor layout: DefaultGeometryProcessor(_P0) + ClampedGradientEffect(_P1) + EmptyXferProcessor(_P2)
// Colorizer: TextureGradientColorizer (16+ stop gradient, texture lookup)
// Permutation dimensions (injected by build tool as #define):
//   LAYOUT_TYPE: 0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND
#version 450

#ifndef LAYOUT_TYPE
#define LAYOUT_TYPE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color_P0;
  vec4 leftBorderColor_P1;
  vec4 rightBorderColor_P1;
#if LAYOUT_TYPE == 2
  float Bias_P1;
  float Scale_P1;
#endif
};

layout(set = 1, binding = 0) uniform sampler2D GradientTexture_P1;

layout(location = 0) in vec2 TransformedCoords_0;

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
  return ((angle * 0.15915494309180001 + 0.5) + Bias_P1) * Scale_P1;
#elif LAYOUT_TYPE == 3
  // Diamond: t = max(|x|, |y|)
  return max(abs(coord.x), abs(coord.y));
#endif
}

vec4 colorize(float t) {
  return texture(GradientTexture_P1, vec2(t, 0.5));
}

void main() {
  vec4 outputColor_P0 = Color_P0;
  highp vec2 coord = TransformedCoords_0;

  float t = computeLayoutT(coord);

  vec4 gradColor;
  if (t <= 0.0) {
    gradColor = leftBorderColor_P1;
  } else if (t >= 1.0) {
    gradColor = rightBorderColor_P1;
  } else {
    gradColor = colorize(t);
  }

  gradColor.rgb *= gradColor.a;
  gradColor *= outputColor_P0.a;

  fragColor = gradColor;
}
