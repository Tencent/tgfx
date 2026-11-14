// The chain's complex rounded-rect vertex hook: device-space vertices like the ellipse, the
// per-corner edge distances, and the half-stroke-width attribute. The attribute is unconditional
// and the StrokeEnabled uniform selects the coverage form, so one layout serves fill and stroke.

#define HAS_COVERAGE 0
#define HAS_UV_COORD 0
#define HAS_SUBSET 0

void ChainComplexGeometry_vertex(inout Vertex v) {
  // ComplexEllipseGeometryProcessor vertices are pre-transformed to device space too.
  v.position = Position;
  v.localCoord = Position;
  vOffsets = InEllipseOffset;
  vRadii = InEllipseRadii;
  vEdgeDist = InEdgeDist;
  vStrokeWidth = InStrokeWidth;
  vColor = InColor;
}
