// RectGeometry fragment hook: the paint color from the uniform and the per-vertex coverage. The
// color source is the former fill pipeline's FILL_COLOR_UNIFORM; the coverage is FILL_COVERAGE_VERTEX.
void RectGeometry_fragment(inout Fragment f) {
  f.color = RectGeometry_Color;
  f.coverage = vec4(RectGeometry_vCoverage);
}
