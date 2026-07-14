// RoundStrokeRectFillShader fragment shader
// Processor layout: RoundStrokeRectGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   HAS_AA: whether coverage-based AA is enabled (ellipse radii for SDF)
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
//   HAS_UV_MATRIX: whether a UV matrix transform is used
#version 450

#ifndef HAS_AA
#define HAS_AA 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#if HAS_COMMON_COLOR || HAS_XP
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if HAS_COMMON_COLOR
  vec4 Color;
#endif
#include "xp_uniforms.inc"
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

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COMMON_COLOR
  vec4 outputColor = Color;
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

#define TGFX_XP_SRC_COLOR (outputColor * outputCoverage)
#include "xp_output.inc"
}
