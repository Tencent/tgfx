// The chain's rect layout with the ChainRectUVGeometry_UVCoord attribute: the vertex hook mirrors the shared text's
// ChainRectUVGeometry_UVCoord branch (the subset corners go through CoordTransformMatrix_0, as the quad GP wrote them)
// and flags CHAIN_UV_COORD_SOURCE so the body's transforms select the leaf source per leaf through
// ChainRectUVGeometry_CoordSourceMask.

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

// The leaf and procedural-source coordinate transforms, the chain body's contract, spelled in
// place: the tools fold NTEX/HAS_SOURCE_COORDS from the profile dim at bake time, and the
// per-leaf source selection stays this geometry's own two-line macros.
#define CHAIN_LEAF_SOURCE(k) (((ChainRectUVGeometry_CoordSourceMask >> (k)) & 1) != 0) ? ChainRectUVGeometry_UVCoord : v.localCoord
#define CHAIN_SOURCE_COORDS (((ChainRectUVGeometry_CoordSourceMask >> 4) & 1) != 0) ? ChainRectUVGeometry_UVCoord : v.localCoord
#if defined(HAS_SOURCE_COORDS)
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
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
