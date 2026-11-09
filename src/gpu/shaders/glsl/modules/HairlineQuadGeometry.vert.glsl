// HairlineQuadGeometry vertex hook: the quad edge data forwarded verbatim. The expressions are
// verbatim from the former kernels/hairline_quad.vert.
void HairlineQuadGeometry_vertex(inout Vertex v) {
  v.position = (HairlineQuadGeometry_Matrix * vec3(HairlineQuadGeometry_Position, 1.0)).xy;
  HairlineQuadGeometry_vHairQuadEdge = HairlineQuadGeometry_InHairQuadEdge;
}
