// The chain's atlas layout vertex hook: position through the ChainAtlasGeometry_Matrix uniform, the glyph mask
// coordinate on the ChainAtlasGeometry_UVCoord attribute (ChainAtlasGeometry_CoordSourceMask picks it per leaf at runtime).

#define HAS_COVERAGE 0
#define HAS_UV_COORD 1
#define HAS_SUBSET 0
#define CHAIN_UV_COORD_SOURCE 1

void ChainAtlasGeometry_vertex(inout Vertex v) {
  highp vec2 position = (ChainAtlasGeometry_Matrix * vec3(ChainAtlasGeometry_Position, 1.0)).xy;
  v.position = position;
  v.localCoord = ChainAtlasGeometry_Position;
  ChainAtlasGeometry_vColor = ChainAtlasGeometry_InColor;

// The leaf and procedural-source coordinate transforms, the chain body's contract, spelled in
// place: the tools fold NTEX/HAS_SOURCE_COORDS from the profile dim at bake time, and the
// per-leaf source selection stays this geometry's own two-line macros.
#define CHAIN_LEAF_SOURCE(k) (((ChainAtlasGeometry_CoordSourceMask >> (k)) & 1) != 0) ? ChainAtlasGeometry_UVCoord : v.localCoord
#define CHAIN_SOURCE_COORDS (((ChainAtlasGeometry_CoordSourceMask >> 4) & 1) != 0) ? ChainAtlasGeometry_UVCoord : v.localCoord
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
