// RRectGeometry vertex hook: forwards the geometry attributes into the varying interface. The
// expressions are verbatim from the former kernels/non_aa_rrect_fill.vert.
void RRectGeometry_vertex(inout Vertex v) {
  v.position = RRectGeometry_Position;
  v.localCoord = RRectGeometry_LocalCoord;
  RRectGeometry_vLocalCoord = v.localCoord;
  RRectGeometry_vRadii = RRectGeometry_Radii;
  RRectGeometry_vRectBounds = RRectGeometry_RectBounds;
  RRectGeometry_vColor = RRectGeometry_Color;
#if RRectGeometry_STROKE
  RRectGeometry_vStrokeWidth = RRectGeometry_StrokeWidth;
#endif
}
