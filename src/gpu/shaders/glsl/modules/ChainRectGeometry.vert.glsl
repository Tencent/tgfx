// The chain's rect layout (uvless) vertex hook. The contract macro names are defined here so the
// shared body text compiles verbatim: GP_LAYOUT selects the body's per-layout branches, and
// HAS_SUBSET aliases the declared SUBSET dim. Vertex::localCoord carries the leaf coordinate
// source the body's transforms read.

#define HAS_COVERAGE 1
#define HAS_UV_COORD 0
#define HAS_SUBSET ChainRectGeometry_SUBSET

void ChainRectGeometry_vertex(inout Vertex v) {
  highp vec2 position = (ChainRectGeometry_Matrix * vec3(ChainRectGeometry_Position, 1.0)).xy;
  v.position = position;
  v.localCoord = ChainRectGeometry_Position;
  ChainRectGeometry_vCoverage = ChainRectGeometry_InCoverage;
  ChainRectGeometry_vColor = ChainRectGeometry_InColor;
#if HAS_SUBSET
  // The subset corners in leaf 0's texture space (the vertex text verbatim; the uvless
  // form goes through its own transform, as the quad GP wrote it).
  highp vec2 subsetLT = (ChainRectGeometry_InTexSubsetMatrix * vec3(ChainRectGeometry_InTexSubset.xy, 1.0)).xy;
  highp vec2 subsetRB = (ChainRectGeometry_InTexSubsetMatrix * vec3(ChainRectGeometry_InTexSubset.zw, 1.0)).xy;
  ChainRectGeometry_vTexSubset = vec4(min(subsetLT, subsetRB), max(subsetLT, subsetRB));
#endif

// The leaf and procedural-source coordinate transforms, the chain body's contract, spelled in
// place: the tools fold NTEX/HAS_SOURCE_COORDS from the profile dim at bake time, and the
// per-leaf source selection stays this geometry's own two-line macros.
#define CHAIN_LEAF_SOURCE(k) ChainRectGeometry_Position
#define CHAIN_SOURCE_COORDS ChainRectGeometry_Position
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
