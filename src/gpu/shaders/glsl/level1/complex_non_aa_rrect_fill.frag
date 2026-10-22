// ComplexNonAARRectFillShader fragment entry: ComplexNonAARRectGeometryProcessor +
// EmptyXferProcessor/PorterDuffXP. Per-corner independent radii SDF evaluation
// (complex_rrect_coverage.inc). The pipeline is the shared dedicated one (fill_fragment_main.inc);
// this family draws without a clip FP, so no clip factor is selected. The uniform block is
// unconditional: the runtime color select (Color / HasCommonColor) must always be declared.
// Fragment dimensions: STROKE, HAS_XP (int, 3). The color source is a runtime select: the exact
// Color uniform when HasCommonColor != 0, the (8-bit quantized) vColor varying otherwise.
#version 450

#ifndef STROKE
#define STROKE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int HasCommonColor;
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 vLocalCoord;
layout(location = 1) in vec4 vXRadii;
layout(location = 2) in vec4 vYRadii;
layout(location = 3) in vec4 vRectBounds;
layout(location = 4) in vec4 vColor;
#if STROKE
layout(location = 5) in vec2 vStrokeWidth;
#endif

#define XP_DST_TEX_BINDING 0

#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#define FILL_COVERAGE FILL_COVERAGE_COMPLEX_RRECT
#include "fill_fragment_main.inc"
