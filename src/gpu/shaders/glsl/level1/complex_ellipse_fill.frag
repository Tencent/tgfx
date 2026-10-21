// ComplexEllipseFillShader fragment entry: ComplexEllipseGeometryProcessor +
// EmptyXferProcessor/PorterDuffXP. Offset-based region dispatch: corner (ellipse SDF), edge
// (linear), center (full) — complex_ellipse_coverage.inc, shared with the pointwise chain's
// GP_LAYOUT 3/4. The device-space mask is a runtime uniform (HasDeviceMask) with a padded dummy
// sampler, not a permutation dimension: ComplexEllipse draws are rare and every extra dimension
// doubles the whole family (stroke x common-color x XP). The pipeline is the shared dedicated one
// (fill_fragment_main.inc); this file declares the family's interface and selects the steps.
// Fragment dimensions: STROKE, HAS_COMMON_COLOR, HAS_XP (int, 3).
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

#if !HAS_COMMON_COLOR
#define FILL_COLOR FILL_COLOR_VARYING
#endif
#define FILL_COVERAGE FILL_COVERAGE_COMPLEX_ELLIPSE
#define FILL_DEVICE_MASK FILL_DEVICE_MASK_RUNTIME
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
