// NonAARRectFillShader fragment shader
// Processor layout: NonAARRectGeometryProcessor(_P0) + EmptyXferProcessor(_P1)
// Permutation dimensions (injected as #define 0/1):
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   STROKE: whether stroke mode is enabled
#version 450

#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef STROKE
#define STROKE 0
#endif

#if HAS_COMMON_COLOR
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color_P0;
};
#endif

layout(location = 0) in vec2 vLocalCoord;
layout(location = 1) in vec2 vRadii;
layout(location = 2) in vec4 vRectBounds;
#if !HAS_COMMON_COLOR
layout(location = 3) in vec4 vColor;
#if STROKE
layout(location = 4) in vec2 vStrokeWidth;
#endif
#else
#if STROKE
layout(location = 3) in vec2 vStrokeWidth;
#endif
#endif

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COMMON_COLOR
  vec4 outputColor = Color_P0;
#else
  vec4 outputColor = vColor;
#endif

  // Outer round rect SDF
  highp vec2 center = (vRectBounds.xy + vRectBounds.zw) * 0.5;
  highp vec2 halfSize = (vRectBounds.zw - vRectBounds.xy) * 0.5;
  highp vec2 q = abs(vLocalCoord - center) - halfSize + vRadii;
  highp float d = min(max(q.x / vRadii.x, q.y / vRadii.y), 0.0) +
                  length(max(q / vRadii, vec2(0.0))) - 1.0;
  highp float outerCoverage = step(d, 0.0);

#if STROKE
  // Inner round rect SDF for stroke
  highp vec2 innerHalfSize = halfSize - 2.0 * vStrokeWidth;
  highp vec2 innerRadii = max(vRadii - 2.0 * vStrokeWidth, vec2(0.0));
  highp float innerCoverage = 0.0;
  if (innerHalfSize.x > 0.0 && innerHalfSize.y > 0.0) {
    highp vec2 qi = abs(vLocalCoord - center) - innerHalfSize + innerRadii;
    highp vec2 safeInnerRadii = max(innerRadii, vec2(0.001));
    highp float di = min(max(qi.x / safeInnerRadii.x, qi.y / safeInnerRadii.y), 0.0) +
                     length(max(qi / safeInnerRadii, vec2(0.0))) - 1.0;
    innerCoverage = step(di, 0.0);
  }
  highp float coverage = outerCoverage * (1.0 - innerCoverage);
#else
  highp float coverage = outerCoverage;
#endif

  fragColor = outputColor * coverage;
}
