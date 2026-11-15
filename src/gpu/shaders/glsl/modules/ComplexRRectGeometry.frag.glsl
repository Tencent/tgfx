// ComplexRRectGeometry fragment hook: the per-vertex color (or the exact Color uniform) and the
// analytic per-corner ellipse SDF coverage. The expressions are bit-exact by contract.
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
