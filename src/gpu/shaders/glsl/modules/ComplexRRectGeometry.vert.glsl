// ComplexRRectGeometry vertex hook: the corner data forwarded to the fragment stage. The position
// is already in device space. The expressions are verbatim from the former
// kernels/complex_non_aa_rrect_fill.vert.
void ComplexRRectGeometry_vertex(inout Vertex v) {
  v.position = ComplexRRectGeometry_Position;
  v.localCoord = ComplexRRectGeometry_LocalCoord;
  ComplexRRectGeometry_vLocalCoord = ComplexRRectGeometry_LocalCoord;
  ComplexRRectGeometry_vXRadii = ComplexRRectGeometry_XRadii;
  ComplexRRectGeometry_vYRadii = ComplexRRectGeometry_YRadii;
  ComplexRRectGeometry_vRectBounds = ComplexRRectGeometry_RectBounds;
  ComplexRRectGeometry_vColor = ComplexRRectGeometry_InColor;
#if ComplexRRectGeometry_STROKE
  ComplexRRectGeometry_vStrokeWidth = ComplexRRectGeometry_StrokeWidth;
#endif
}
