// Shared vertex implementation of PointwiseChainShader.
// Feeds one TransformedCoords varying per texture leaf of the pointwise DAG. The leaf count is the
// TEXTURE_COUNT permutation (0 -> 0 leaves, 1 -> the four-leaf slots) because the varying layout is
// part of the vertex/fragment interface.
// Permutation dimensions (vert): GP_LAYOUT, HAS_COVERAGE, HAS_UV_COORD, TEXTURE_COUNT,
// HAS_SUBSET.
// The color attribute is unconditional on every layout (a white filler where the buffer is shared
// across draws); the exact common color rides the Color uniform through the HasCommonColor
// runtime uniform, so HAS_COLOR is not a dimension.
// GP_LAYOUT: 0 = rect family (DefaultGP and QuadPerEdgeAAGP share it — the position always goes
// through the Matrix uniform, which DefaultGP fills with the view matrix and QuadPerEdgeAAGP with
// identity, bit-exact), 1 = EllipseGeometryProcessor (device-space vertices, carries the ellipse
// edge varyings whose per-pixel coverage is evaluated in the fragment stage), 2 =
// HairlineLineGeometryProcessor (position through the Matrix uniform, carries the edge-distance
// varying; leaf coordinates come from the transformed position, as the runtime GP emits them),
// 3 / 4 = ComplexEllipseGeometryProcessor fill / stroke (device-space vertices like the ellipse,
// plus the per-corner edge distances; the stroke form adds the half stroke width attribute, which
// is why fill and stroke are two layouts rather than a runtime flag).

#ifndef GP_LAYOUT
#define GP_LAYOUT 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_UV_COORD
#define HAS_UV_COORD 0
#endif
#ifndef TEXTURE_COUNT
#define TEXTURE_COUNT 0
#endif
#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif

#if TEXTURE_COUNT == 0
#define NTEX 0
#else
#define NTEX 4
#endif

#if GP_LAYOUT == 1
// EllipseGP: the locations live in gp_ellipse_layout.inc, shared with the dedicated kernel.
#include "geometry/gp_ellipse_layout.inc"
layout(location = GP_ELLIPSE_IN_COLOR) in vec4 inColor;
layout(location = GP_ELLIPSE_IN_OFFSET) in vec2 inEllipseOffset;
layout(location = GP_ELLIPSE_IN_RADII) in vec4 inEllipseRadii;
#define CHAIN_TEX_LOC_BASE GP_ELLIPSE_VARYING_COUNT
#elif GP_LAYOUT == 2
// HairlineLineGP attribute order: position, edgeDistance, color. The edge-distance varying
// occupies location 0, so the leaf coordinates start at 1.
layout(location = 2) in vec4 inColor;
#define CHAIN_TEX_LOC_BASE 1
#elif GP_LAYOUT >= 3
// ComplexEllipseGP: the locations live in gp_complex_ellipse_layout.inc, shared with the
// dedicated kernel.
#include "geometry/gp_complex_ellipse_layout.inc"
layout(location = GP_COMPLEX_IN_COLOR) in vec4 inColor;
layout(location = GP_COMPLEX_IN_OFFSET) in vec2 inEllipseOffset;
layout(location = GP_COMPLEX_IN_RADII) in vec4 inEllipseRadii;
layout(location = GP_COMPLEX_IN_EDGE_DIST) in vec2 inEdgeDist;
#if GP_LAYOUT == 4
layout(location = GP_COMPLEX_IN_STROKE) in vec2 inStrokeWidth;
#endif
#else
#define CHAIN_TEX_LOC_BASE 0
#endif
// The procedural-source varying; must agree with pointwise_source_coords.inc.
#ifndef CHAIN_SOURCE_COORDS_LOC
#define CHAIN_SOURCE_COORDS_LOC 7
#endif
// The per-quad subset varying (rect layout only: leaves 0-3, coverage 4, color 5); must agree with
// pointwise_fragment_interface.inc.
#define CHAIN_SUBSET_LOC 6

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if GP_LAYOUT == 0 || GP_LAYOUT == 2
  mat3 Matrix;
#endif
#ifdef TGFX_POINTWISE_SOURCE_COORDS
  mat3 CoordTransformMatrix_0;
#if NTEX > 0
  mat3 CoordTransformMatrix_1;
  mat3 CoordTransformMatrix_2;
  mat3 CoordTransformMatrix_3;
  mat3 CoordTransformMatrix_4;
  mat3 CoordTransformMatrix_5;
#endif
#else
#if NTEX > 0
  mat3 CoordTransformMatrix_0;
  mat3 CoordTransformMatrix_1;
  mat3 CoordTransformMatrix_2;
  mat3 CoordTransformMatrix_3;
  mat3 CoordTransformMatrix_4;
#endif
#endif
#if GP_LAYOUT == 0 && HAS_UV_COORD
  // Per-target coordinate source: bit k set picks the uvCoord attribute for leaf k, bit 4 for the
  // procedural source (gradient or noise, SourceCoords); a clear bit sources from aPosition. Atlas
  // text sources the atlas leaf from maskCoord (riding the uvCoord slot) but gradients and image
  // leaves from position; every
  // other GP writes all-ones, which reproduces the previous shared coordSource behavior.
  int CoordSourceMask;
#endif
#if HAS_SUBSET && !HAS_UV_COORD
  // Leaf 0's coordinate transform, written by the quad GP for the subset corners; with uvCoord the
  // corners go through CoordTransformMatrix_0 itself, as in the runtime GP.
  mat3 texSubsetMatrix;
#endif
};

#if GP_LAYOUT == 0
layout(location = 0) in vec2 aPosition;
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
#endif
#if HAS_UV_COORD
// uvCoord follows the coverage slot when present (quads, meshes); atlas text buffers carry no
// coverage, so maskCoord lands directly after the position.
layout(location = 1 + HAS_COVERAGE) in vec2 uvCoord;
#endif
layout(location = 1 + HAS_COVERAGE + HAS_UV_COORD) in vec4 inColor;
#if HAS_SUBSET
// QuadPerEdgeAAGP only: the color slot is unconditional, so the subset follows it.
layout(location = 2 + HAS_COVERAGE + HAS_UV_COORD) in vec4 texSubset;
#endif
#elif GP_LAYOUT == 2
layout(location = 0) in vec2 aPosition;
layout(location = 1) in float aEdgeDistance;
#else
layout(location = 0) in vec2 inPosition;
#endif

#if GP_LAYOUT == 1
layout(location = GP_ELLIPSE_V_OFFSETS) out vec2 vEllipseOffsets;
layout(location = GP_ELLIPSE_V_RADII) out vec4 vEllipseRadii;
layout(location = GP_ELLIPSE_V_COLOR) out vec4 vColor;
#elif GP_LAYOUT == 2
layout(location = 0) out float vEdgeDistance;
layout(location = NTEX + 1) out vec4 vColor;
#elif GP_LAYOUT >= 3
layout(location = GP_COMPLEX_V_OFFSETS) out vec2 vEllipseOffsets;
layout(location = GP_COMPLEX_V_RADII) out vec4 vEllipseRadii;
layout(location = GP_COMPLEX_V_EDGE_DIST) out vec2 vEdgeDist;
#if GP_LAYOUT == 4
layout(location = GP_COMPLEX_V_STROKE) out vec2 vStrokeWidth;
#endif
layout(location = GP_COMPLEX_V_COLOR) out vec4 vColor;
#else
#if HAS_COVERAGE
layout(location = NTEX) out float vCoverage;
#endif
layout(location = NTEX + 1) out vec4 vColor;
#endif

#if NTEX >= 1
layout(location = CHAIN_TEX_LOC_BASE + 0) out vec3 TransformedCoords_0;
#endif
#if NTEX >= 2
layout(location = CHAIN_TEX_LOC_BASE + 1) out vec3 TransformedCoords_1;
#endif
#if NTEX >= 4
layout(location = CHAIN_TEX_LOC_BASE + 2) out vec3 TransformedCoords_2;
layout(location = CHAIN_TEX_LOC_BASE + 3) out vec3 TransformedCoords_3;
#endif
#ifdef TGFX_POINTWISE_SOURCE_COORDS
layout(location = CHAIN_SOURCE_COORDS_LOC) out vec3 SourceCoords;
#endif
#if HAS_SUBSET
layout(location = CHAIN_SUBSET_LOC) out vec4 vTexSubset;
#endif

void main() {
#if GP_LAYOUT == 0 && HAS_UV_COORD
#define CHAIN_LEAF_SOURCE(k) ((((CoordSourceMask >> (k)) & 1) != 0) ? uvCoord : aPosition)
#define CHAIN_SOURCE_COORDS ((((CoordSourceMask >> 4) & 1) != 0) ? uvCoord : aPosition)
#elif GP_LAYOUT == 0
#define CHAIN_LEAF_SOURCE(k) aPosition
#define CHAIN_SOURCE_COORDS aPosition
#elif GP_LAYOUT == 2
// GLSLHairlineLineGeometryProcessor feeds emitTransforms the transformed position; the uvMatrix
// it folds into every coord transform maps that back to local space.
#define CHAIN_LEAF_SOURCE(k) position
#define CHAIN_SOURCE_COORDS position
#else
#define CHAIN_LEAF_SOURCE(k) inPosition
#define CHAIN_SOURCE_COORDS inPosition
#endif
#if GP_LAYOUT == 0
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  vColor = inColor;
#elif GP_LAYOUT == 2
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  vEdgeDistance = aEdgeDistance;
  vColor = inColor;
#elif GP_LAYOUT == 1
  // EllipseGeometryProcessor vertices are pre-transformed to device space.
  highp vec2 position = inPosition;
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
  vColor = inColor;
#else
  // ComplexEllipseGeometryProcessor vertices are pre-transformed to device space too.
  highp vec2 position = inPosition;
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
  vEdgeDist = inEdgeDist;
#if GP_LAYOUT == 4
  vStrokeWidth = inStrokeWidth;
#endif
  vColor = inColor;
#endif
#ifdef TGFX_POINTWISE_SOURCE_COORDS
#if NTEX > 0
  TransformedCoords_0 = (CoordTransformMatrix_1 * vec3(CHAIN_LEAF_SOURCE(0), 1.0)).xyz;
  TransformedCoords_1 = (CoordTransformMatrix_2 * vec3(CHAIN_LEAF_SOURCE(1), 1.0)).xyz;
  TransformedCoords_2 = (CoordTransformMatrix_3 * vec3(CHAIN_LEAF_SOURCE(2), 1.0)).xyz;
  TransformedCoords_3 = (CoordTransformMatrix_4 * vec3(CHAIN_LEAF_SOURCE(3), 1.0)).xyz;
#endif
  SourceCoords = (CoordTransformMatrix_0 * vec3(CHAIN_SOURCE_COORDS, 1.0)).xyz;
#else
#if NTEX > 0
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(CHAIN_LEAF_SOURCE(0), 1.0)).xyz;
  TransformedCoords_1 = (CoordTransformMatrix_1 * vec3(CHAIN_LEAF_SOURCE(1), 1.0)).xyz;
  TransformedCoords_2 = (CoordTransformMatrix_2 * vec3(CHAIN_LEAF_SOURCE(2), 1.0)).xyz;
  TransformedCoords_3 = (CoordTransformMatrix_3 * vec3(CHAIN_LEAF_SOURCE(3), 1.0)).xyz;
#endif
#endif
#if GP_LAYOUT == 0 && HAS_COVERAGE
  vCoverage = inCoverage;
#endif
#if HAS_SUBSET
  // The subset corners in leaf 0's texture space (GLSLQuadPerEdgeAAGeometryProcessor's
  // onEmitTransform; projective draws stay on the runtime route).
#if HAS_UV_COORD
  highp vec2 subsetLT = (CoordTransformMatrix_0 * vec3(texSubset.xy, 1.0)).xy;
  highp vec2 subsetRB = (CoordTransformMatrix_0 * vec3(texSubset.zw, 1.0)).xy;
#else
  highp vec2 subsetLT = (texSubsetMatrix * vec3(texSubset.xy, 1.0)).xy;
  highp vec2 subsetRB = (texSubsetMatrix * vec3(texSubset.zw, 1.0)).xy;
#endif
  vTexSubset = vec4(min(subsetLT, subsetRB), max(subsetLT, subsetRB));
#endif
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
