// HairlineQuadGeometry fragment hook: the exact paint color, the Loop-Blinn edge coverage times the
// draw coverage and the analytic clip. The expressions are verbatim from the former fill
// pipeline's FILL_COVERAGE_HAIRLINE_QUAD and FILL_CLIP.
#include "geometry/hairline_quad_coverage.inc"
#define Rect HairlineQuadGeometry_Rect
#define HasClip HairlineQuadGeometry_HasClip
#define LocalRect HairlineQuadGeometry_LocalRect
#define RadiiX HairlineQuadGeometry_RadiiX
#define RadiiY HairlineQuadGeometry_RadiiY
#define AntiAlias HairlineQuadGeometry_AntiAlias
#define DeviceToLocal HairlineQuadGeometry_DeviceToLocal
#include "coverage/clip_coverage.inc"
#undef Rect
#undef HasClip
#undef LocalRect
#undef RadiiX
#undef RadiiY
#undef AntiAlias
#undef DeviceToLocal

void HairlineQuadGeometry_fragment(inout Fragment f) {
  f.color = HairlineQuadGeometry_Color;
  highp float coverage = hairlineQuadEdgeCoverage(HairlineQuadGeometry_vHairQuadEdge,
                                              HairlineQuadGeometry_Coverage,
                                              HairlineQuadGeometry_AAEnabled);
  coverage *= clipCoverage();
  f.coverage = vec4(coverage);
}
