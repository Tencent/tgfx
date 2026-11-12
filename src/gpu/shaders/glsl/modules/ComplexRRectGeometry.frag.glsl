// ComplexRRectGeometry fragment hook: the per-vertex color (or the exact Color uniform) and the
// analytic per-corner ellipse SDF coverage. The call shape is verbatim from the former fill
// pipeline's FILL_COVERAGE_COMPLEX_RRECT, with the stroke form selected by StrokeEnabled at runtime.
#include "geometry/complex_rrect_coverage.inc"

void ComplexRRectGeometry_fragment(inout Fragment f) {
  f.color = ComplexRRectGeometry_vColor;
  if (ComplexRRectGeometry_HasCommonColor != 0) {
    f.color = ComplexRRectGeometry_Color;
  }
  f.coverage = vec4(complexRRectCoverage(
      ComplexRRectGeometry_vLocalCoord, ComplexRRectGeometry_vRectBounds,
      ComplexRRectGeometry_vXRadii, ComplexRRectGeometry_vYRadii,
      ComplexRRectGeometry_vStrokeWidth, ComplexRRectGeometry_StrokeEnabled));
}
