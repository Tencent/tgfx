// YUVTexture vertex hook: the source coordinate through the leaf transform. The expressions are
// bit-exact by contract.
void YUVTexture_vertex(inout Vertex v) {
  YUVTexture_vCoord = YUVTexture_CoordMatrix * vec3(v.localCoord, 1.0);
}
