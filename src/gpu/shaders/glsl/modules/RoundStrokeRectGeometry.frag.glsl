// RoundStrokeRectGeometry fragment hook: the per-vertex color (or the exact Color uniform) and the
// AA edge coverage scaled by the vertex coverage. The analytic clip rides the family's
// AnalyticClip slot, which multiplies it in after this hook.
#include "geometry/round_stroke_rect_coverage.inc"

void RoundStrokeRectGeometry_fragment(inout Fragment f) {
  f.color = RoundStrokeRectGeometry_vColor;
  if (RoundStrokeRectGeometry_HasCommonColor != 0) {
    f.color = RoundStrokeRectGeometry_Color;
  }
  highp float coverage =
      roundStrokeRectCoverage(RoundStrokeRectGeometry_vOffsets, RoundStrokeRectGeometry_vRadii,
                              RoundStrokeRectGeometry_vCoverage,
                              RoundStrokeRectGeometry_AAEnabled != 0);
  f.coverage = vec4(coverage);
}
