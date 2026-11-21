// ComplexRRectGeometry vertex hook: the corner data forwarded to the fragment stage. The position
// is already in device space. The stroke width attribute is unconditional and the StrokeEnabled
// uniform selects the coverage form, so one variant serves the fill and the stroke form. The
// expressions are bit-exact by contract.

void ComplexRRectGeometry_vertex(inout Vertex v) {
  v.position = ComplexRRectGeometry_Position;
  v.localCoord = ComplexRRectGeometry_LocalCoord;
  ComplexRRectGeometry_vLocalCoord = ComplexRRectGeometry_LocalCoord;
  ComplexRRectGeometry_vXRadii = ComplexRRectGeometry_XRadii;
  ComplexRRectGeometry_vYRadii = ComplexRRectGeometry_YRadii;
  ComplexRRectGeometry_vRectBounds = ComplexRRectGeometry_RectBounds;
  ComplexRRectGeometry_vColor = ComplexRRectGeometry_InColor;
  ComplexRRectGeometry_vStrokeWidth = ComplexRRectGeometry_StrokeWidth;

// The leaf and procedural-source coordinate transforms, the chain body's contract, spelled in
// place: the tools fold NTEX/HAS_SOURCE_COORDS from the profile dim at bake time.
#if defined(HAS_SOURCE_COORDS)
#if NTEX > 0
  TransformedCoords_0 = (CoordTransformMatrix_1 * vec3(v.localCoord, 1.0)).xyz;
  TransformedCoords_1 = (CoordTransformMatrix_2 * vec3(v.localCoord, 1.0)).xyz;
  TransformedCoords_2 = (CoordTransformMatrix_3 * vec3(v.localCoord, 1.0)).xyz;
  TransformedCoords_3 = (CoordTransformMatrix_4 * vec3(v.localCoord, 1.0)).xyz;
#endif
  SourceCoords = (CoordTransformMatrix_0 * vec3(v.localCoord, 1.0)).xyz;
#else
#if NTEX > 0
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(v.localCoord, 1.0)).xyz;
  TransformedCoords_1 = (CoordTransformMatrix_1 * vec3(v.localCoord, 1.0)).xyz;
  TransformedCoords_2 = (CoordTransformMatrix_2 * vec3(v.localCoord, 1.0)).xyz;
  TransformedCoords_3 = (CoordTransformMatrix_3 * vec3(v.localCoord, 1.0)).xyz;
#endif
#endif
}
