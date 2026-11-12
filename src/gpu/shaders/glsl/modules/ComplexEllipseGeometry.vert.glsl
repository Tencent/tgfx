// ComplexEllipseGeometry vertex hook: the ellipse data forwarded to the fragment stage. The stroke
// width attribute is unconditional and the StrokeEnabled uniform selects the coverage form, so one
// variant serves the fill and the stroke form. The expressions are verbatim from the former
// kernels/complex_ellipse_fill.vert.
void ComplexEllipseGeometry_vertex(inout Vertex v) {
  v.position = ComplexEllipseGeometry_Position;
  ComplexEllipseGeometry_vOffsets = ComplexEllipseGeometry_InEllipseOffset;
  ComplexEllipseGeometry_vRadii = ComplexEllipseGeometry_InEllipseRadii;
  ComplexEllipseGeometry_vEdgeDist = ComplexEllipseGeometry_InEdgeDist;
  ComplexEllipseGeometry_vColor = ComplexEllipseGeometry_InColor;
  ComplexEllipseGeometry_vStrokeWidth = ComplexEllipseGeometry_InStrokeWidth;
}
