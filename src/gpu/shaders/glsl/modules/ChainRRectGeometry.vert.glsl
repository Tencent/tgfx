// The chain's non-AA rounded-rect layout vertex hook (GP_LAYOUT 5): device-space vertices like the
// RRect family's, the local coord and radii the per-pixel SDF reads, and the half-stroke-width
// attribute. The attribute is unconditional and the StrokeEnabled uniform selects the coverage
// form, so one layout serves fill and stroke.

#define GP_LAYOUT 5
#define HAS_COVERAGE 0
#define HAS_UV_COORD 0
#define HAS_SUBSET 0

void ChainRRectGeometry_vertex(inout Vertex v) {
  v.position = inPosition;
  v.localCoord = inLocalCoord;
  vLocalCoord = inLocalCoord;
  vRadii = inRadii;
  vRectBounds = inRectBounds;
  vColor = inColor;
  vStrokeWidth = inStrokeWidth;
}
