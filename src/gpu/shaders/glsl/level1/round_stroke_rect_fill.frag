// RoundStrokeRectFillShader fragment entry: RoundStrokeRectGeometryProcessor +
// EmptyXferProcessor/PorterDuffXP. The normalized-offset ellipse SDF edge
// (round_stroke_rect_coverage.inc) scales the per-vertex coverage when HAS_AA, a hard inside step
// otherwise. The analytic draw clip (AA rect or rounded rect) rides the shared runtime clip contract
// uniforms, so it costs no variant: the draw carries the clip FP only for its uniforms. The block is
// unconditional because the contract fields must always be declared. The pipeline is the shared
// dedicated one (fill_fragment_main.inc).
// Fragment dimensions: HAS_AA, HAS_XP (int, 3). HAS_UV_MATRIX stays vertex-only: the fragment
// stage never branches on it. The color source is a runtime select: the exact Color uniform when
// HasCommonColor != 0, the (8-bit quantized) vColor varying otherwise.
#version 450

#ifndef HAS_AA
#define HAS_AA 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int HasCommonColor;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 vEllipseOffsets;
#if HAS_AA
layout(location = 1) in float vCoverage;
layout(location = 2) in vec2 vEllipseRadii;
layout(location = 3) in vec4 vColor;
#else
layout(location = 1) in vec4 vColor;
#endif

#define XP_DST_TEX_BINDING 0

#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#define FILL_COVERAGE FILL_COVERAGE_ROUND_STROKE
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
