// EllipseGeometry vertex hook: the ellipse data forwarded to the fragment stage. The expressions
// are bit-exact by contract.
void EllipseGeometry_vertex(inout Vertex v) {
  v.position = EllipseGeometry_Position;
  // The raw position doubles as the local coordinate for any body module that maps it (the glass
  // family's background-coordinate transform); the Ellipse family's own body reads none.
  v.localCoord = EllipseGeometry_Position;
  EllipseGeometry_vOffsets = EllipseGeometry_InEllipseOffset;
  EllipseGeometry_vRadii = EllipseGeometry_InEllipseRadii;
  EllipseGeometry_vColor = EllipseGeometry_InColor;
}
