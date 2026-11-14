// The chain's hairline-line layout vertex hook: position through the Matrix uniform, the edge
// distance varying, and the color. The leaf coordinate source is the transformed position, as the
// runtime hairline GP feeds its transforms (its uvMatrix maps back to local space).

#define GP_LAYOUT 2
#define HAS_COVERAGE 0
#define HAS_UV_COORD 0
#define HAS_SUBSET 0

void ChainHairlineGeometry_vertex(inout Vertex v) {
  highp vec2 position = (Matrix * vec3(Position, 1.0)).xy;
  v.position = position;
  v.localCoord = position;
  vEdge = InEdge;
  vColor = InColor;
}
