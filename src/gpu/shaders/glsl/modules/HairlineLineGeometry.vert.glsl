// HairlineLineGeometry vertex hook: the edge distance forwarded verbatim. The expressions are
// bit-exact by contract.
void HairlineLineGeometry_vertex(inout Vertex v) {
  v.position = (HairlineLineGeometry_Matrix * vec3(HairlineLineGeometry_Position, 1.0)).xy;
  HairlineLineGeometry_vEdge = HairlineLineGeometry_InEdge;
  HairlineLineGeometry_vColor = HairlineLineGeometry_InColor;
}
