// Texture vertex hook: the source coordinate through the leaf transform, and the per-vertex subset
// bounds through the same transform (SubsetMatrix carries the coord transform's value when the
// geometry supplies uv coordinates, so one field serves both paths). The expressions are verbatim
// from the former kernels/quad_texture_fill.vert.
void Texture_vertex(inout Vertex v) {
  Texture_vCoord = Texture_CoordMatrix * vec3(v.localCoord, 1.0);
#if Texture_HAS_SUBSET
  highp vec2 srcLT = QuadTextureGeometry_vTexSubset.xy;
  highp vec2 srcRB = QuadTextureGeometry_vTexSubset.zw;
  highp vec2 perspLT = (Texture_SubsetMatrix * vec3(srcLT, 1.0)).xy;
  highp vec2 perspRB = (Texture_SubsetMatrix * vec3(srcRB, 1.0)).xy;
  Texture_vTexSubset = vec4(min(perspLT, perspRB), max(perspLT, perspRB));
#endif
}
