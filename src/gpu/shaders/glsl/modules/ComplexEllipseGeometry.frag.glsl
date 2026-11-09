// ComplexEllipseGeometry fragment hook: the per-vertex color (or the exact Color uniform) and the
// analytic complex-ellipse coverage. The call shapes are verbatim from the former fill pipeline's
// FILL_COLOR_VARYING_OR_COMMON and FILL_COVERAGE_COMPLEX_ELLIPSE.
#include "geometry/complex_ellipse_coverage.inc"

void ComplexEllipseGeometry_fragment(inout Fragment f) {
  f.color = ComplexEllipseGeometry_vColor;
  if (ComplexEllipseGeometry_HasCommonColor != 0) {
    f.color = ComplexEllipseGeometry_Color;
  }
#if ComplexEllipseGeometry_STROKE
  f.coverage = vec4(complexEllipseEdgeCoverage(
      ComplexEllipseGeometry_vOffsets, ComplexEllipseGeometry_vRadii,
      ComplexEllipseGeometry_vEdgeDist, ComplexEllipseGeometry_vStrokeWidth, true));
#else
  f.coverage = vec4(complexEllipseEdgeCoverage(
      ComplexEllipseGeometry_vOffsets, ComplexEllipseGeometry_vRadii,
      ComplexEllipseGeometry_vEdgeDist, vec2(0.0), false));
#endif
}
