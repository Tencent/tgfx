// HairlineQuadGeometry vertex hook: the quad edge data forwarded verbatim. The expressions are
// bit-exact by contract.

void HairlineQuadGeometry_vertex(inout Vertex v) {
  v.position = (HairlineQuadGeometry_Matrix * vec3(HairlineQuadGeometry_Position, 1.0)).xy;
  v.localCoord = v.position;
  HairlineQuadGeometry_vHairQuadEdge = HairlineQuadGeometry_InHairQuadEdge;
  HairlineQuadGeometry_vColor = vec4(0.0);

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
