// ComplexEllipseGeometry fragment hook: the per-vertex color (or the exact Color uniform) and the
// analytic complex-ellipse coverage. The expressions are bit-exact by contract.
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
