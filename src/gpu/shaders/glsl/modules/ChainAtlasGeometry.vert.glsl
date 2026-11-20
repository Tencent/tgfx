// The chain's atlas layout vertex hook: position through the ChainAtlasGeometry_Matrix uniform, the glyph mask
// coordinate on the ChainAtlasGeometry_UVCoord attribute (ChainAtlasGeometry_CoordSourceMask picks it per leaf at runtime).

#include "pipeline/chain/pointwise_declared_config.inc"

#define HAS_COVERAGE 0
#define HAS_UV_COORD 1
#define HAS_SUBSET 0
#define CHAIN_UV_COORD_SOURCE 1

void ChainAtlasGeometry_vertex(inout Vertex v) {
  highp vec2 position = (ChainAtlasGeometry_Matrix * vec3(ChainAtlasGeometry_Position, 1.0)).xy;
  v.position = position;
  v.localCoord = ChainAtlasGeometry_Position;
  ChainAtlasGeometry_vColor = ChainAtlasGeometry_InColor;

// The leaf and procedural-source coordinate transforms, the chain body's contract: the macros name
// this geometry's own coordinate source (the rect-uv and atlas layouts select per leaf at runtime
// through their CoordSourceMask), the shared include spells the transforms once.
#define CHAIN_LEAF_SOURCE(k) (((ChainAtlasGeometry_CoordSourceMask >> (k)) & 1) != 0) ? ChainAtlasGeometry_UVCoord : v.localCoord
#define CHAIN_SOURCE_COORDS (((ChainAtlasGeometry_CoordSourceMask >> 4) & 1) != 0) ? ChainAtlasGeometry_UVCoord : v.localCoord
#include "pipeline/chain/pointwise_vertex_coords.inc"
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
