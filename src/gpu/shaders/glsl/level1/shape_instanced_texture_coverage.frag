#version 450

#ifndef GRADIENT
#define GRADIENT 0
#endif
// An analytic clip (AA rect / rrect) rides the runtime clip contract uniforms, so a draw carrying
// a clip coverage FP next to its coverage texture selects the same variant.
#define HAS_RUNTIME_CLIP 1

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Subset;
  int AlphaOnly;
#include "coverage_uniforms.inc"
#if GRADIENT
  // Single-interval gradient parameters, written by the gradient FP's onSetData.
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
layout(location = 1) in vec4 vColor;
#if GRADIENT
layout(location = 2) in vec2 GradientCoords;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(location = 0) out vec4 fragColor;

#if GRADIENT
#include "gradient_layout.inc"
#endif
#include "clip_coverage.inc"

void main() {
  highp vec2 finalCoord = clamp(TransformedCoords_0, Subset.xy, Subset.zw);
  vec4 coverage = texture(TextureSampler_0, finalCoord);
  if (AlphaOnly != 0) {
    coverage = vec4(coverage.r);
  }
  // The runtime multiplies the clip coverage into the total coverage before the color is
  // modulated by it, so the clip lands here rather than on fragColor.
  if (HasClip != 0) {
    coverage *= clipCoverage();
  }
#if GRADIENT
  // The runtime gradient emission: border clamp, premultiply, then modulate by the input
  // color's alpha (the per-instance vertex color; white for colorless batches).
  highp float t = gradientLayoutT(LayoutType, Bias, Scale, GradientCoords);
  vec4 gradColor;
  if (t <= 0.0) {
    gradColor = leftBorderColor;
  } else if (t >= 1.0) {
    gradColor = rightBorderColor;
  } else {
    gradColor = mix(start, end, t);
  }
  gradColor.rgb *= gradColor.a;
  gradColor *= vColor.a;
  fragColor = gradColor * coverage;
#else
  fragColor = vColor * coverage;
#endif
}
