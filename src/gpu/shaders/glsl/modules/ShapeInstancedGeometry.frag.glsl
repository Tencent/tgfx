// ShapeInstancedGeometry fragment hook: the per-instance color and the per-vertex coverage.
void ShapeInstancedGeometry_fragment(inout Fragment f) {
  f.color = ShapeInstancedGeometry_vColor;
  f.coverage = vec4(ShapeInstancedGeometry_vCoverage);
}
