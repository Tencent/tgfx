// The chain's non-AA rounded-rect layout vertex hook (GP_LAYOUT 5 fill / 6 stroke): device-space
// vertices like the RRect family's, the local coord and radii the per-pixel SDF reads, and the
// half-stroke-width attribute of the stroke form.

#define GP_LAYOUT (5 + STROKE)
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
#if GP_LAYOUT == 6
  vStrokeWidth = inStrokeWidth;
#endif
}
