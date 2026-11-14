// The chain's per-corner-radius non-AA rounded-rect layout vertex hook (GP_LAYOUT 7): device-space
// vertices, the corner radii and bounds the per-pixel SDF reads, and the half-stroke-width
// attribute. The attribute is unconditional and the StrokeEnabled uniform selects the coverage
// form, so one layout serves fill and stroke.

#define HAS_COVERAGE 0
#define HAS_UV_COORD 0
#define HAS_SUBSET 0

void ChainComplexRRectGeometry_vertex(inout Vertex v) {
  v.position = Position;
  v.localCoord = LocalCoord;
  vLocalCoord = LocalCoord;
  vXRadii = XRadii;
  vYRadii = YRadii;
  vRectBounds = RectBounds;
  vColor = InColor;
  vStrokeWidth = InStrokeWidth;
}
