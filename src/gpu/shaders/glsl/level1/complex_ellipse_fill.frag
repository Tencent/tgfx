// ComplexEllipseFillShader fragment shader
// Processor layout: ComplexEllipseGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   STROKE: whether stroke mode is enabled
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
// Uses offset-based region dispatch: corner (ellipse SDF), edge (linear), center (full).
#version 450

#ifndef STROKE
#define STROKE 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1
#ifndef HAS_DEVICE_MASK
#define HAS_DEVICE_MASK 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if HAS_COMMON_COLOR
  vec4 Color;
#endif
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
layout(location = 2) in vec2 vEdgeDist;
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

// A device-space mask coverage FP binds its sampler first, so the terminal XP dst texture shifts
// one binding up (same layout EllipseFillShader uses).
#if HAS_DEVICE_MASK
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 1
#else
#define XP_DST_TEX_BINDING 0
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "clip_coverage.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COMMON_COLOR
  vec4 outputColor = Color;
#else
  vec4 outputColor = vColor;
#endif

  highp vec2 offset = vEllipseOffsets;
  highp vec2 eDist = vEdgeDist;

  bool hasXCurve = offset.x > 0.0;
  bool hasYCurve = offset.y > 0.0;

  highp float outerAlpha;
  if (hasXCurve && hasYCurve) {
    highp vec2 safeOffset = max(offset, vec2(1.0 / 4096.0));
#if STROKE
    safeOffset *= vEllipseRadii.xy;
#endif
    highp float test = dot(safeOffset, safeOffset) - 1.0;
    highp vec2 grad = 2.0 * safeOffset * vEllipseRadii.xy;
    highp float gradDot = max(dot(grad, grad), 1.1755e-38);
    highp float invlen = inversesqrt(gradDot);
    outerAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);
  } else if (hasYCurve) {
    outerAlpha = clamp(0.5 + eDist.y, 0.0, 1.0);
  } else if (hasXCurve) {
    outerAlpha = clamp(0.5 + eDist.x, 0.0, 1.0);
  } else {
    outerAlpha = 1.0;
  }

#if STROKE
  highp vec2 sw = vStrokeWidth;
  highp float innerAlpha;
  if (hasXCurve && hasYCurve) {
    highp vec2 innerOffset = max(offset, vec2(1.0 / 4096.0)) * vEllipseRadii.zw;
    highp float innerTest = dot(innerOffset, innerOffset) - 1.0;
    highp vec2 innerGrad = 2.0 * innerOffset * vEllipseRadii.zw;
    highp float innerGradDot = max(dot(innerGrad, innerGrad), 1.1755e-38);
    highp float innerInvlen = inversesqrt(innerGradDot);
    innerAlpha = clamp(0.5 + innerTest * innerInvlen, 0.0, 1.0);
  } else if (hasYCurve) {
    innerAlpha = clamp(0.5 - (eDist.y - 2.0 * sw.y), 0.0, 1.0);
  } else if (hasXCurve) {
    innerAlpha = clamp(0.5 - (eDist.x - 2.0 * sw.x), 0.0, 1.0);
  } else {
    highp float innerEdgeX = eDist.x - 2.0 * sw.x;
    highp float innerEdgeY = eDist.y - 2.0 * sw.y;
    innerAlpha = clamp(0.5 - min(innerEdgeX, innerEdgeY), 0.0, 1.0);
  }
  outerAlpha *= innerAlpha;
#endif

// The uncovered color rides TGFX_COVERAGE_SRC_COLOR and the analytic edge coverage enters as the
// initial coverage; coverage_output.inc multiplies the runtime clip coverage (rrect/AA-rect, the
// shared clip contract uniforms) on top and hands xp_output.inc the uncovered color plus the
// TOTAL coverage. With HasClip == 0 the result is identical to the previous direct
// outputColor * outerAlpha composition.
#define TGFX_COVERAGE_SRC_COLOR outputColor
#define TGFX_INITIAL_COVERAGE vec4(outerAlpha)
#include "coverage_output.inc"
#include "xp_output.inc"
}
