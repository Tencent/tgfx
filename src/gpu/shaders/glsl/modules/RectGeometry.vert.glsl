// RectGeometry vertex hook: the view transform plus the raw position forwarded as the module
// local coordinate (the texture modules map it through their own coord transforms). The
// expressions are verbatim from the former kernels/solid_color_fill.vert.
void RectGeometry_vertex(inout Vertex v) {
  v.position = (RectGeometry_Matrix * vec3(RectGeometry_Position, 1.0)).xy;
  v.localCoord = RectGeometry_Position;
  RectGeometry_vCoverage = RectGeometry_Coverage;
}
