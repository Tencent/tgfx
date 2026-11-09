// QuadColorGeometry fragment hook: the per-vertex color, or the exact Color uniform when
// HasCommonColor is set. The expressions are verbatim from the former fill pipeline's
// FILL_COLOR_VARYING_OR_COMMON and FILL_COVERAGE_VERTEX.
void QuadColorGeometry_fragment(inout Fragment f) {
  f.color = QuadColorGeometry_vColor;
  if (QuadColorGeometry_HasCommonColor != 0) {
    f.color = QuadColorGeometry_Color;
  }
  f.coverage = vec4(QuadColorGeometry_vCoverage);
}
