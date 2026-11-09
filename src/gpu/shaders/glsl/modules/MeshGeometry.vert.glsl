// MeshGeometry vertex hook: the view transform plus the texture coordinate chain. The expressions
// are verbatim from the former kernels/mesh_fill.vert.
void MeshGeometry_vertex(inout Vertex v) {
  v.position = (MeshGeometry_Matrix * vec3(MeshGeometry_Position, 1.0)).xy;
#if MeshGeometry_TEX_COORDS
  v.localCoord = MeshGeometry_TexCoord;
#else
  v.localCoord = MeshGeometry_Position;
#endif
  MeshGeometry_vCoord = (MeshGeometry_CoordMatrix * vec3(v.localCoord, 1.0)).xy;
  MeshGeometry_vColor = MeshGeometry_InColor;
  MeshGeometry_vCoverage = MeshGeometry_Coverage;
}
