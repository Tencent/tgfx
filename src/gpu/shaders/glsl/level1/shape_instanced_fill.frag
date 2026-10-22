// ShapeInstancedFillShader fragment entry: ShapeInstancedGeometryProcessor +
// EmptyXferProcessor/PorterDuffXP. Instanced shape fills: the shape's own coverage is the
// vCoverage varying (a constant 1.0 for non-AA path triangles, so the modulation is the identity
// there), the color is per-instance (vColor — colorless batches broadcast white, matching the
// runtime codegen's implicit color). The pipeline is the shared dedicated one
// (fill_fragment_main.inc).
// Fragment dimensions: HAS_XP (int, 3).
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

#if HAS_XP
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#include "xp_uniforms.inc"
};
#endif

layout(location = 0) in vec2 TransformedCoords_0;
layout(location = 1) in float vCoverage;
layout(location = 2) in vec4 vColor;

#define XP_DST_TEX_BINDING 0

#define FILL_COLOR FILL_COLOR_VARYING
#define FILL_COVERAGE FILL_COVERAGE_VERTEX
#include "fill_fragment_main.inc"
