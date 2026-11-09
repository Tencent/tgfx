// QuadGeometry fragment hook: the paint color from the uniform and the per-vertex coverage.
void QuadGeometry_fragment(inout Fragment f) {
  f.color = QuadGeometry_Color;
  f.coverage = vec4(QuadGeometry_vCoverage);
}
