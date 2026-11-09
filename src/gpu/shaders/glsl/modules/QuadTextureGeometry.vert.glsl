// QuadTextureGeometry vertex hook: the per-vertex data forwarded to the fragment stage and the
// source coordinate handed to the body modules (the per-vertex uv coordinate when the geometry
// carries one, the position otherwise). The expressions are verbatim from the former
// kernels/quad_texture_fill.vert.
void QuadTextureGeometry_vertex(inout Vertex v) {
  v.position = QuadTextureGeometry_Position;
#if QuadTextureGeometry_HAS_UV_COORD
  v.localCoord = QuadTextureGeometry_UVCoord;
#else
  v.localCoord = QuadTextureGeometry_Position;
#endif
  QuadTextureGeometry_vCoverage = QuadTextureGeometry_InCoverage;
  QuadTextureGeometry_vColor = QuadTextureGeometry_InColor;
#if QuadTextureGeometry_HAS_SUBSET
  QuadTextureGeometry_vTexSubset = QuadTextureGeometry_InTexSubset;
#endif
}
