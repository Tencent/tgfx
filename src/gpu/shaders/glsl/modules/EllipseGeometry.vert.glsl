// EllipseGeometry vertex hook: the ellipse data forwarded to the fragment stage. The expressions
// are verbatim from the former kernels/ellipse_fill.vert.
void EllipseGeometry_vertex(inout Vertex v) {
  v.position = EllipseGeometry_Position;
  EllipseGeometry_vOffsets = EllipseGeometry_InEllipseOffset;
  EllipseGeometry_vRadii = EllipseGeometry_InEllipseRadii;
  EllipseGeometry_vColor = EllipseGeometry_InColor;
}
