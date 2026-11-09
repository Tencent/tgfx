// MaskTexture vertex hook: the coverage mask's coordinate through its coord transform. The
// expression is verbatim from the former kernels/plain_texture.vert.
void MaskTexture_vertex(inout Vertex v) {
  MaskTexture_vCoord = (MaskTexture_CoordMatrix * vec3(v.localCoord, 1.0)).xy;
}
