// QuadColorGeometry vertex hook: the position is already in device space; the raw position doubles
// as the module local coordinate. The expressions are bit-exact by contract.
void QuadColorGeometry_vertex(inout Vertex v) {
  v.position = QuadColorGeometry_Position;
  v.localCoord = QuadColorGeometry_Position;
  QuadColorGeometry_vCoverage = QuadColorGeometry_Coverage;
  QuadColorGeometry_vColor = QuadColorGeometry_InColor;
}
