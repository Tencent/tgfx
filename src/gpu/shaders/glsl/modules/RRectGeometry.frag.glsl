// RRectGeometry fragment hook: seeds the color (the runtime varying-or-common select) and the
// hard rounded-rect coverage (the stroke's inner SDF included). The expressions are verbatim from
// the former fill_fragment_main.inc steps FILL_COLOR_VARYING_OR_COMMON and FILL_COVERAGE_RRECT.
#include "geometry/rrect_coverage.inc"

void RRectGeometry_fragment(inout Fragment f) {
  vec4 outputColor = RRectGeometry_vColor;
  if (RRectGeometry_HasCommonColor != 0) {
    outputColor = RRectGeometry_CommonColor;
  }
  f.color = outputColor;
#if RRectGeometry_STROKE
  f.coverage = vec4(nonAARRectCoverage(RRectGeometry_vLocalCoord, RRectGeometry_vRectBounds,
                                       RRectGeometry_vRadii, RRectGeometry_vStrokeWidth, true));
#else
  f.coverage = vec4(nonAARRectCoverage(RRectGeometry_vLocalCoord, RRectGeometry_vRectBounds,
                                       RRectGeometry_vRadii, vec2(0.0), false));
#endif
}
