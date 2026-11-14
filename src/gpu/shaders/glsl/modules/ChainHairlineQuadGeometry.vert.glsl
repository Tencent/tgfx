// The chain's hairline-quad layout vertex hook (GP_LAYOUT 9): the Loop-Blinn quad edge data and the
// view transform. The provider carries no color attribute, so vColor is the white filler the body's
// color macro needs; the exact color always rides the Color uniform.

#define GP_LAYOUT 9
#define HAS_COVERAGE 0
#define HAS_UV_COORD 0
#define HAS_SUBSET 0

void ChainHairlineQuadGeometry_vertex(inout Vertex v) {
  highp vec2 position = (Matrix * vec3(Position, 1.0)).xy;
  v.position = position;
  v.localCoord = position;
  vHairQuadEdge = InHairQuadEdge;
  vColor = vec4(1.0);
}
