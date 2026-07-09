// RoundStrokeRectFillShader fragment shader
// Processor layout: RoundStrokeRectGeometryProcessor(_P0) + EmptyXferProcessor(_P1)
// Permutation dimensions (injected as #define 0/1):
//   HAS_AA: whether coverage-based AA is enabled (ellipse radii for SDF)
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   HAS_UV_MATRIX: whether a UV matrix transform is used
#version 450

#ifndef HAS_AA
#define HAS_AA 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif

#if HAS_COMMON_COLOR
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color_P0;
};
#endif

layout(location = 0) in vec2 vEllipseOffsets;
#if HAS_AA
layout(location = 1) in float vCoverage;
layout(location = 2) in vec2 vEllipseRadii;
#if !HAS_COMMON_COLOR
layout(location = 3) in vec4 vColor;
#endif
#else
#if !HAS_COMMON_COLOR
layout(location = 1) in vec4 vColor;
#endif
#endif

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COMMON_COLOR
  vec4 outputColor = Color_P0;
#else
  vec4 outputColor = vColor;
#endif

  highp vec2 offset = vEllipseOffsets;

#if HAS_AA
  vec4 outputCoverage = vec4(vCoverage);
  highp float test = dot(offset, offset) - 1.0;
  if (test > -0.5) {
    highp vec2 grad = 2.0 * offset * vEllipseRadii;
    highp float gradDot = dot(grad, grad);
    gradDot = max(gradDot, 1.1755e-38);
    highp float invlen = inversesqrt(gradDot);
    highp float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);
    outputCoverage *= edgeAlpha;
  }
#else
  highp float test = dot(offset, offset);
  highp float edgeAlpha = step(test, 1.0);
  vec4 outputCoverage = vec4(edgeAlpha);
#endif

  fragColor = outputColor * outputCoverage;
}
