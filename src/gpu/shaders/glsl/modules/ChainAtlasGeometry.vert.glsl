// The chain's atlas layout vertex hook: position through the Matrix uniform, the glyph mask
// coordinate on the UVCoord attribute (CoordSourceMask picks it per leaf at runtime).

#define HAS_COVERAGE 0
#define HAS_UV_COORD 1
#define HAS_SUBSET 0
#define CHAIN_UV_COORD_SOURCE 1

void ChainAtlasGeometry_vertex(inout Vertex v) {
  highp vec2 position = (Matrix * vec3(Position, 1.0)).xy;
  v.position = position;
  v.localCoord = Position;
  vColor = InColor;
}
