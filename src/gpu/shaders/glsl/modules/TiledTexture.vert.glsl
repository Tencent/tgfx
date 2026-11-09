// TiledTexture vertex hook: the texture coordinate is the local coordinate through the module's
// transform matrix. The expression is verbatim from the former kernels/non_aa_rrect_fill.vert.
void TiledTexture_vertex(inout Vertex v) {
  TiledTexture_vCoord = (TiledTexture_CoordMatrix * vec3(v.localCoord, 1.0)).xy;
}
