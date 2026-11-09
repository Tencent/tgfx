// YUVTexture vertex hook: the source coordinate through the leaf transform. The expressions are
// verbatim from the former kernels/yuv_texture_fill.vert.
void YUVTexture_vertex(inout Vertex v) {
  YUVTexture_vCoord = YUVTexture_CoordMatrix * vec3(v.localCoord, 1.0);
}
