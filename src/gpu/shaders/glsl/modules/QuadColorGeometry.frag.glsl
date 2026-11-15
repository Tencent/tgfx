// QuadColorGeometry fragment hook: the per-vertex color, or the exact Color uniform when
// HasCommonColor is set. The expressions are bit-exact by contract.
void QuadColorGeometry_fragment(inout Fragment f) {
  f.color = QuadColorGeometry_vColor;
  if (QuadColorGeometry_HasCommonColor != 0) {
    f.color = QuadColorGeometry_Color;
  }
  f.coverage = vec4(QuadColorGeometry_vCoverage);
}
