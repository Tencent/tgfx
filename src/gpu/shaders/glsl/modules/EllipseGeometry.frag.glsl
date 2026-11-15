// EllipseGeometry fragment hook: the per-vertex color (or the exact Color uniform) and the
// analytic ellipse edge coverage. The expressions are bit-exact by contract.
#include "geometry/ellipse_coverage.inc"

void EllipseGeometry_fragment(inout Fragment f) {
  f.color = EllipseGeometry_vColor;
  if (EllipseGeometry_HasCommonColor != 0) {
    f.color = EllipseGeometry_Color;
  }
  f.coverage = vec4(ellipseEdgeCoverage(EllipseGeometry_vOffsets, EllipseGeometry_vRadii,
                                        EllipseGeometry_StrokeEnabled));
}
