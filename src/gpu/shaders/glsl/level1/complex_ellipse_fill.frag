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
// The device-space mask is a runtime uniform (HasDeviceMask) with a padded dummy sampler, not a
// permutation dimension: ComplexEllipse draws are rare and every extra dimension doubles the whole
// family (stroke x common-color x XP).
#define HAS_RUNTIME_DEVICE_MASK 1

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

// The mask sampler is always declared so the runtime branch needs no second variant; when the draw
// carries no mask FP the binding is padded with the shared dummy texture (ProgramInfo).
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "clip_coverage.inc"
#include "complex_ellipse_coverage.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COMMON_COLOR
  vec4 outputColor = Color;
#else
  vec4 outputColor = vColor;
#endif

#if STROKE
  highp float outerAlpha =
      complexEllipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, vEdgeDist, vStrokeWidth, true);
#else
  highp float outerAlpha =
      complexEllipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, vEdgeDist, vec2(0.0), false);
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
