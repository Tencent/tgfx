// The chain's per-corner-radius non-AA rounded-rect layout vertex hook (GP_LAYOUT 7 fill / 8
// stroke): device-space vertices, the corner radii and bounds the per-pixel SDF reads, and the
// half-stroke-width attribute of the stroke form.

#define GP_LAYOUT (7 + STROKE)
#define HAS_COVERAGE 0
#define HAS_UV_COORD 0
#define HAS_SUBSET 0

void ChainComplexRRectGeometry_vertex(inout Vertex v) {
  v.position = inPosition;
  v.localCoord = inLocalCoord;
  vLocalCoord = inLocalCoord;
  vXRadii = inXRadii;
  vYRadii = inYRadii;
  vRectBounds = inRectBounds;
  vColor = inColor;
#if GP_LAYOUT == 8
  vStrokeWidth = inStrokeWidth;
#endif
}
