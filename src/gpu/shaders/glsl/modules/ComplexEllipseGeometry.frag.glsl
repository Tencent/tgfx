// ComplexEllipseGeometry fragment hook: the per-vertex color (or the exact Color uniform) and the
// analytic complex-ellipse coverage. The call shape is verbatim from the former fill pipeline's
// FILL_COLOR_VARYING_OR_COMMON and FILL_COVERAGE_COMPLEX_ELLIPSE, with the stroke form selected by
// StrokeEnabled at runtime.
#include "geometry/complex_ellipse_coverage.inc"

void ComplexEllipseGeometry_fragment(inout Fragment f) {
  f.color = ComplexEllipseGeometry_vColor;
  if (ComplexEllipseGeometry_HasCommonColor != 0) {
    f.color = ComplexEllipseGeometry_Color;
  }
  f.coverage = vec4(complexEllipseEdgeCoverage(
      ComplexEllipseGeometry_vOffsets, ComplexEllipseGeometry_vRadii,
      ComplexEllipseGeometry_vEdgeDist, ComplexEllipseGeometry_vStrokeWidth,
      ComplexEllipseGeometry_StrokeEnabled));
}
