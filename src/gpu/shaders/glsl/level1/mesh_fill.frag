// MeshFillShader fragment entry: MeshGeometryProcessor + EmptyXferProcessor/PorterDuffXP. A mesh
// draw carries optional per-vertex colors (vColor) and coverage (vCoverage); without per-vertex
// colors the color is the Color uniform. The pipeline is the shared dedicated one
// (fill_fragment_main.inc); this file declares the family's interface and selects the steps.
// Fragment dimensions: HAS_COLOR, HAS_COVERAGE, HAS_XP (int, 3).
#version 450

#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#if !HAS_COLOR || HAS_XP
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if !HAS_COLOR
  vec4 Color;
#endif
#include "xp_uniforms.inc"
};
#endif

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_COLOR
layout(location = 1) in vec4 vColor;
#endif
#if HAS_COVERAGE
layout(location = 2) in float vCoverage;
#endif

#define XP_DST_TEX_BINDING 0

#if HAS_COLOR
#define FILL_COLOR FILL_COLOR_VARYING
#else
#define FILL_COLOR FILL_COLOR_UNIFORM
#endif
#if HAS_COVERAGE
#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#endif
#include "fill_fragment_main.inc"
