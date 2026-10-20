// Fragment stage of the pointwise kernels: shared core, part 2 of 3, varyings, samplers and the
// shared helper libraries. The procedural-source coordinate varying belongs to the source modules
// (pointwise_source_coords.inc).

#if GP_LAYOUT == 1
layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
#if HAS_COLOR
layout(location = 2) in vec4 vColor;
#define CHAIN_TEX_LOC_BASE 3
#else
#define CHAIN_TEX_LOC_BASE 2
#endif
#elif GP_LAYOUT == 2
layout(location = 0) in float vEdgeDistance;
#define CHAIN_TEX_LOC_BASE 1
#elif GP_LAYOUT >= 3
// ComplexEllipse fill (3) / stroke (4); the locations live in pointwise_complex_ellipse_layout.inc.
#include "pointwise_complex_ellipse_layout.inc"
layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
layout(location = 2) in vec2 vEdgeDist;
#if GP_LAYOUT == 4
layout(location = CHAIN_COMPLEX_STROKE_LOC) in vec2 vStrokeWidth;
#endif
#if HAS_COLOR
layout(location = CHAIN_COMPLEX_COLOR_LOC) in vec4 vColor;
#endif
#else
#define CHAIN_TEX_LOC_BASE 0
#endif

#if GP_LAYOUT == 0 && HAS_COVERAGE
layout(location = NTEX) in float vCoverage;
#endif
#if GP_LAYOUT == 0 && HAS_COLOR
// QuadGP only: per-vertex color (broadcast for common-color draws) is the geometry color source.
layout(location = NTEX + 1) in vec4 vColor;
#endif

#if HAS_SUBSET
// The per-quad subset of a strict draw, in leaf 0's texture space (pointwise_vertex.inc).
layout(location = 6) in vec4 vTexSubset;
#endif

#if NTEX >= 1
layout(location = CHAIN_TEX_LOC_BASE + 0) in vec3 TransformedCoords_0;
#endif
#if NTEX >= 2
layout(location = CHAIN_TEX_LOC_BASE + 1) in vec3 TransformedCoords_1;
#endif
#if NTEX >= 4
layout(location = CHAIN_TEX_LOC_BASE + 2) in vec3 TransformedCoords_2;
layout(location = CHAIN_TEX_LOC_BASE + 3) in vec3 TransformedCoords_3;
#endif

#if NTEX >= 1
layout(set = 1, binding = 0) uniform CHAIN_LEAF_SAMPLER TextureSampler_0;
#endif
#if NTEX >= 2
layout(set = 1, binding = 1) uniform CHAIN_LEAF_SAMPLER TextureSampler_1;
#endif
#if NTEX >= 4
layout(set = 1, binding = 2) uniform CHAIN_LEAF_SAMPLER TextureSampler_2;
layout(set = 1, binding = 3) uniform CHAIN_LEAF_SAMPLER TextureSampler_3;
#endif
#if NTEX > 0 || HAS_MASK_TEXTURE
// Device-space alpha mask applied after the DAG and before the XP stage (same application point
// as the legacy blend kernel). Four-leaf variants always bind it (phantom when absent).
layout(set = 1, binding = NTEX) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING (NTEX + 1)
#else
#define XP_DST_TEX_BINDING NTEX
#endif

// The chain kernel blends regardless of XP state, so include the shared blend math directly;
// xp_porter_duff.inc re-includes it under HAS_XP >= 1, guarded against double definition.
#include "ellipse_coverage.inc"
#include "hairline_line_coverage.inc"
#include "complex_ellipse_coverage.inc"
#include "xp_blend_colors.inc"
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "xp_blend_pass.inc"

#include "pointwise_tiled_sampling.inc"
