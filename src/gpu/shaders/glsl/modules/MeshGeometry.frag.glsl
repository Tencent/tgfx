// MeshGeometry fragment hook: the per-vertex color, or the exact Color uniform when
// HasCommonColor is set (meshes without per-vertex colors); the coverage is the per-vertex
// channel. The expressions are bit-exact by contract.
void MeshGeometry_fragment(inout Fragment f) {
  f.color = MeshGeometry_vColor;
  if (MeshGeometry_HasCommonColor != 0) {
    f.color = MeshGeometry_Color;
  }
  f.coverage = vec4(MeshGeometry_vCoverage);
}
