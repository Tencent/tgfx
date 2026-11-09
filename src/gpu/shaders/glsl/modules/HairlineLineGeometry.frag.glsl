// HairlineLineGeometry fragment hook: the exact paint color, the edge alpha times the draw
// coverage and the analytic clip. The expressions are verbatim from the former fill pipeline's
// FILL_COVERAGE_HAIRLINE_LINE and FILL_CLIP.
#include "geometry/hairline_line_coverage.inc"
#define Rect HairlineLineGeometry_Rect
#define HasClip HairlineLineGeometry_HasClip
#define LocalRect HairlineLineGeometry_LocalRect
#define RadiiX HairlineLineGeometry_RadiiX
#define RadiiY HairlineLineGeometry_RadiiY
#define AntiAlias HairlineLineGeometry_AntiAlias
#define DeviceToLocal HairlineLineGeometry_DeviceToLocal
#include "coverage/clip_coverage.inc"
#undef Rect
#undef HasClip
#undef LocalRect
#undef RadiiX
#undef RadiiY
#undef AntiAlias
#undef DeviceToLocal

void HairlineLineGeometry_fragment(inout Fragment f) {
  f.color = HairlineLineGeometry_Color;
  highp float coverage = hairlineLineEdgeCoverage(HairlineLineGeometry_vEdge,
                                                  HairlineLineGeometry_Coverage,
                                                  HairlineLineGeometry_AAEnabled);
  coverage *= clipCoverage();
  f.coverage = vec4(coverage);
}
