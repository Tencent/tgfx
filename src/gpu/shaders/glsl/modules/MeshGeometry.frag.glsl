// MeshGeometry fragment hook: the per-vertex color, or the exact Color uniform when
// HasCommonColor is set (meshes without per-vertex colors). The expressions are verbatim from the
// former fill pipeline's FILL_COLOR_VARYING_OR_COMMON and FILL_COVERAGE_VERTEX.
void MeshGeometry_fragment(inout Fragment f) {
  f.color = MeshGeometry_vColor;
  if (MeshGeometry_HasCommonColor != 0) {
    f.color = MeshGeometry_Color;
  }
  f.coverage = vec4(MeshGeometry_vCoverage);
}
