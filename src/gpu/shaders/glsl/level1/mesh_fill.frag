// MeshFillShader fragment entry: MeshGeometryProcessor + EmptyXferProcessor/PorterDuffXP. A mesh
// draw carries optional per-vertex colors (vColor) and coverage (vCoverage); the color attribute is
// unconditional (white for meshes without per-vertex colors), and colorless draws pick the exact
// Color uniform through the HasCommonColor runtime uniform. The pipeline is the shared dedicated
// one (fill_fragment_main.inc); this file declares the family's interface and selects the steps.
// Fragment dimensions: HAS_COVERAGE, HAS_XP (int, 3).
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  // Exact paint color for colorless draws. The per-vertex color attribute is UByte4Normalized
  // and loses precision on fractional colors, so the exact Color uniform is preferred when
  // HasCommonColor is set; otherwise the vColor varying is used (matching the runtime codegen,
  // which reads the per-vertex attribute only for meshes with per-vertex colors).
  vec4 Color;
  int HasCommonColor;
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
layout(location = 1) in vec4 vColor;
#if HAS_COVERAGE
layout(location = 2) in float vCoverage;
#endif

#define XP_DST_TEX_BINDING 0

#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#if HAS_COVERAGE
#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#endif
#include "fill_fragment_main.inc"
