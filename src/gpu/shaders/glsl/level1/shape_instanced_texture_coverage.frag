#version 450

#ifndef GRADIENT
#define GRADIENT 0
#endif
#ifndef HAS_COLORS
#define HAS_COLORS 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Subset;
  int AlphaOnly;
#if GRADIENT
  // Single-interval gradient parameters, written by the gradient FP's onSetData (same names as
  // SingleIntervalGradientShader).
  vec4 leftBorderColor;
  vec4 rightBorderColor;
  int LayoutType;
  float Bias;
  float Scale;
  vec4 start;
  vec4 end;
#endif
};

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_COLORS
layout(location = 1) in vec4 vColor;
#endif
#if GRADIENT
layout(location = 2) in vec2 GradientCoords;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(location = 0) out vec4 fragColor;

#if GRADIENT
// Layout math verbatim from the single-interval gradient kernel.
float computeLayoutT(vec2 coord) {
  if (LayoutType == 1) {
    return length(coord);
  } else if (LayoutType == 2) {
    float angle = atan(-coord.y, -coord.x);
    return ((angle * 0.15915494309180001 + 0.5) + Bias) * Scale;
  } else if (LayoutType == 3) {
    return max(abs(coord.x), abs(coord.y));
  }
  return coord.x + 1.0000000000000001e-05;
}
#endif

void main() {
  highp vec2 finalCoord = clamp(TransformedCoords_0, Subset.xy, Subset.zw);
  vec4 coverage = texture(TextureSampler_0, finalCoord);
  if (AlphaOnly != 0) {
    coverage = vec4(coverage.r);
  }
#if GRADIENT
  // The runtime gradient emission: border clamp, premultiply, then modulate by the input
  // color's alpha (here the per-instance vertex color).
  highp float t = computeLayoutT(GradientCoords);
  vec4 gradColor;
  if (t <= 0.0) {
    gradColor = leftBorderColor;
  } else if (t >= 1.0) {
    gradColor = rightBorderColor;
  } else {
    gradColor = mix(start, end, t);
  }
  gradColor.rgb *= gradColor.a;
#if HAS_COLORS
  gradColor *= vColor.a;
#endif
  fragColor = gradColor * coverage;
#else
  fragColor = vColor * coverage;
#endif
}
