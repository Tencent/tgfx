// The chain's rect layout with the ChainRectUVGeometry_UVCoord attribute: the vertex hook mirrors the shared text's
// ChainRectUVGeometry_UVCoord branch (the subset corners go through CoordTransformMatrix_0, as the quad GP wrote them)
// and flags CHAIN_UV_COORD_SOURCE so the body's transforms select the leaf source per leaf through
// ChainRectUVGeometry_CoordSourceMask.

#include "pipeline/chain/pointwise_declared_config.inc"

#define HAS_COVERAGE 1
#define HAS_UV_COORD 1
#define HAS_SUBSET ChainRectUVGeometry_SUBSET
#define CHAIN_UV_COORD_SOURCE 1

void ChainRectUVGeometry_vertex(inout Vertex v) {
  highp vec2 position = (ChainRectUVGeometry_Matrix * vec3(ChainRectUVGeometry_Position, 1.0)).xy;
  v.position = position;
  v.localCoord = ChainRectUVGeometry_Position;
  ChainRectUVGeometry_vCoverage = ChainRectUVGeometry_InCoverage;
  ChainRectUVGeometry_vColor = ChainRectUVGeometry_InColor;
#if HAS_SUBSET
  highp vec2 subsetLT = (CoordTransformMatrix_0 * vec3(ChainRectUVGeometry_InTexSubset.xy, 1.0)).xy;
  highp vec2 subsetRB = (CoordTransformMatrix_0 * vec3(ChainRectUVGeometry_InTexSubset.zw, 1.0)).xy;
  ChainRectUVGeometry_vTexSubset = vec4(min(subsetLT, subsetRB), max(subsetLT, subsetRB));
#endif

// The leaf and procedural-source coordinate transforms, the chain body's contract: the macros name
// this geometry's own coordinate source (the rect-uv and atlas layouts select per leaf at runtime
// through their CoordSourceMask), the shared include spells the transforms once.
#define CHAIN_LEAF_SOURCE(k) (((ChainRectUVGeometry_CoordSourceMask >> (k)) & 1) != 0) ? ChainRectUVGeometry_UVCoord : v.localCoord
#define CHAIN_SOURCE_COORDS (((ChainRectUVGeometry_CoordSourceMask >> 4) & 1) != 0) ? ChainRectUVGeometry_UVCoord : v.localCoord
#include "pipeline/chain/pointwise_vertex_coords.inc"
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
