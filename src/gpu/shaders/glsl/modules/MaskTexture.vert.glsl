// MaskTexture vertex hook: the coverage mask's coordinate through its coord transform. The
// expression is bit-exact by contract.
void MaskTexture_vertex(inout Vertex v) {
  MaskTexture_vCoord = (MaskTexture_CoordMatrix * vec3(v.localCoord, 1.0)).xy;
}
