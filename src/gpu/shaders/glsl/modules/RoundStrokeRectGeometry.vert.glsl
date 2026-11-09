// RoundStrokeRectGeometry vertex hook: the ellipse data and the per-vertex coverage forwarded to
// the fragment stage. The position is already in device space. The expressions are verbatim from
// the former kernels/round_stroke_rect_fill.vert.
void RoundStrokeRectGeometry_vertex(inout Vertex v) {
  v.position = RoundStrokeRectGeometry_Position;
  RoundStrokeRectGeometry_vOffsets = RoundStrokeRectGeometry_InEllipseOffset;
  RoundStrokeRectGeometry_vCoverage = RoundStrokeRectGeometry_InCoverage;
  RoundStrokeRectGeometry_vRadii = RoundStrokeRectGeometry_InEllipseRadii;
  RoundStrokeRectGeometry_vColor = RoundStrokeRectGeometry_InColor;
}
