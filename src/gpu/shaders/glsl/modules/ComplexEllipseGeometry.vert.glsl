// ComplexEllipseGeometry vertex hook: the ellipse data forwarded to the fragment stage. The
// expressions are verbatim from the former kernels/complex_ellipse_fill.vert.
void ComplexEllipseGeometry_vertex(inout Vertex v) {
  v.position = ComplexEllipseGeometry_Position;
  ComplexEllipseGeometry_vOffsets = ComplexEllipseGeometry_InEllipseOffset;
  ComplexEllipseGeometry_vRadii = ComplexEllipseGeometry_InEllipseRadii;
  ComplexEllipseGeometry_vEdgeDist = ComplexEllipseGeometry_InEdgeDist;
  ComplexEllipseGeometry_vColor = ComplexEllipseGeometry_InColor;
#if ComplexEllipseGeometry_STROKE
  ComplexEllipseGeometry_vStrokeWidth = ComplexEllipseGeometry_InStrokeWidth;
#endif
}
