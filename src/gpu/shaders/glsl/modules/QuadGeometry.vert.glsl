// QuadGeometry vertex hook: the position is already in device space, so it skips the view
// transform; the raw position doubles as the module local coordinate. The expressions are
// verbatim from the former kernels/quad_color_fill.vert.
void QuadGeometry_vertex(inout Vertex v) {
  v.position = QuadGeometry_Position;
  v.localCoord = QuadGeometry_Position;
  QuadGeometry_vCoverage = QuadGeometry_Coverage;
}
