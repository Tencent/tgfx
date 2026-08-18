// EllipseFillShader fragment shader
// Processor layout: EllipseGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
//   HAS_DEVICE_MASK: device-space mask texture binding is present
#version 450

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
  int StrokeEnabled;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
#if !HAS_COMMON_COLOR
layout(location = 2) in vec4 vColor;
#endif

#if HAS_DEVICE_MASK
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 1
#else
#define XP_DST_TEX_BINDING 0
#endif
#include "ellipse_coverage.inc"
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COMMON_COLOR
  vec4 outputColor = Color;
#else
  vec4 outputColor = vColor;
#endif

  // Outer curve coverage using ellipse distance approximation
  highp float edgeAlpha = ellipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, StrokeEnabled);

  // The uncovered color rides TGFX_COVERAGE_SRC_COLOR and the edge antialiasing enters as the
  // initial coverage; coverage_output.inc multiplies clip and device-mask coverage on top and
  // hands xp_output.inc the uncovered color (TGFX_XP_SRC_UNPREMUL) plus the TOTAL coverage.
  // Baking edgeAlpha into the source color instead double-applies it on the XferProcessor path.
#define TGFX_COVERAGE_SRC_COLOR outputColor
#define TGFX_INITIAL_COVERAGE vec4(edgeAlpha)
#include "coverage_output.inc"
#include "xp_output.inc"
}
