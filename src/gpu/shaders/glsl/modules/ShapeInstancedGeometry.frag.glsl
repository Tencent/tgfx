// ShapeInstancedGeometry fragment hook: the per-instance color and the per-vertex coverage (the
// former fill pipeline's FILL_COLOR_VARYING and FILL_COVERAGE_VERTEX).
void ShapeInstancedGeometry_fragment(inout Fragment f) {
  f.color = ShapeInstancedGeometry_vColor;
  f.coverage = vec4(ShapeInstancedGeometry_vCoverage);
}
