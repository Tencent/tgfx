// ComplexNonAARRectFillShader fragment entry: ComplexNonAARRectGeometryProcessor +
// EmptyXferProcessor/PorterDuffXP. Per-corner independent radii SDF evaluation
// (complex_rrect_coverage.inc). The pipeline is the shared dedicated one (fill_fragment_main.inc);
// this family draws without a clip FP, so no clip factor is selected.
// Fragment dimensions: HAS_COMMON_COLOR, STROKE, HAS_XP (int, 3).
#version 450

#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef STROKE
#define STROKE 0
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

layout(location = 0) in vec2 vLocalCoord;
layout(location = 1) in vec4 vXRadii;
layout(location = 2) in vec4 vYRadii;
layout(location = 3) in vec4 vRectBounds;
#if !HAS_COMMON_COLOR
layout(location = 4) in vec4 vColor;
#if STROKE
layout(location = 5) in vec2 vStrokeWidth;
#endif
#else
#if STROKE
layout(location = 4) in vec2 vStrokeWidth;
#endif
#endif

#define XP_DST_TEX_BINDING 0

#if !HAS_COMMON_COLOR
#define FILL_COLOR FILL_COLOR_VARYING
#endif
#define FILL_COVERAGE FILL_COVERAGE_COMPLEX_RRECT
#include "fill_fragment_main.inc"
