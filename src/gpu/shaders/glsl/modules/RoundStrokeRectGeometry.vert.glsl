// RoundStrokeRectGeometry vertex hook: the ellipse data and the per-vertex coverage forwarded to
// the fragment stage. The position is already in device space. The expressions are verbatim from
// the reference emission.

void RoundStrokeRectGeometry_vertex(inout Vertex v) {
  v.position = RoundStrokeRectGeometry_Position;
  v.localCoord = v.position;
  RoundStrokeRectGeometry_vOffsets = RoundStrokeRectGeometry_InEllipseOffset;
  RoundStrokeRectGeometry_vCoverage = RoundStrokeRectGeometry_InCoverage;
  RoundStrokeRectGeometry_vRadii = RoundStrokeRectGeometry_InEllipseRadii;
  RoundStrokeRectGeometry_vColor = RoundStrokeRectGeometry_InColor;

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
