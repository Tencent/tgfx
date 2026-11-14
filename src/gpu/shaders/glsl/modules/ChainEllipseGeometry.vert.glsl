// The chain's ellipse layout vertex hook: device-space vertices (no Matrix), the edge varyings the
// body evaluates per pixel, and the color.

#define GP_LAYOUT 1
#define HAS_COVERAGE 0
#define HAS_UV_COORD 0
#define HAS_SUBSET 0

void ChainEllipseGeometry_vertex(inout Vertex v) {
  // EllipseGeometryProcessor vertices are pre-transformed to device space.
  v.position = Position;
  v.localCoord = Position;
  vOffsets = InEllipseOffset;
  vRadii = InEllipseRadii;
  vColor = InColor;
}
