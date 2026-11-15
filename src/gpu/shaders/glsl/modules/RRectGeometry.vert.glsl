// RRectGeometry vertex hook: forwards the geometry attributes into the varying interface. The
// stroke width attribute is unconditional and the StrokeEnabled uniform selects the coverage form,
// so one variant serves the fill and the stroke form. The expressions are bit-exact by contract.

void RRectGeometry_vertex(inout Vertex v) {
  v.position = RRectGeometry_Position;
  v.localCoord = RRectGeometry_LocalCoord;
  RRectGeometry_vLocalCoord = v.localCoord;
  RRectGeometry_vRadii = RRectGeometry_Radii;
  RRectGeometry_vRectBounds = RRectGeometry_RectBounds;
  RRectGeometry_vColor = RRectGeometry_Color;
  RRectGeometry_vStrokeWidth = RRectGeometry_StrokeWidth;
}
