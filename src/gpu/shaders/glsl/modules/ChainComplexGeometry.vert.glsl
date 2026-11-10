// The chain's complex rounded-rect vertex hook: device-space vertices like the ellipse, the
// per-corner edge distances, and the half-stroke-width attribute of the stroke form (GP_LAYOUT 4).

#define GP_LAYOUT (3 + STROKE)
#define HAS_COVERAGE 0
#define HAS_UV_COORD 0
#define HAS_SUBSET 0

void ChainComplexGeometry_vertex(inout Vertex v) {
  // ComplexEllipseGeometryProcessor vertices are pre-transformed to device space too.
  v.position = inPosition;
  v.localCoord = inPosition;
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
  vEdgeDist = inEdgeDist;
#if GP_LAYOUT == 4
  vStrokeWidth = inStrokeWidth;
#endif
  vColor = inColor;
}
