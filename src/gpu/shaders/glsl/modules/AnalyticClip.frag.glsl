// AnalyticClip fragment hook: the analytic draw clip (AA rect or rounded rect) through the shared
// runtime clip contract, selected by the HasClip uniform. The expressions are verbatim from the
// former fill_fragment_main.inc step FILL_CLIP via coverage/clip_coverage.inc; the alias defines
// map the clip contract's field names onto this module's.
#define Rect AnalyticClip_Rect
#define HasClip AnalyticClip_HasClip
#define LocalRect AnalyticClip_LocalRect
#define RadiiX AnalyticClip_RadiiX
#define RadiiY AnalyticClip_RadiiY
#define AntiAlias AnalyticClip_AntiAlias
#define DeviceToLocal AnalyticClip_DeviceToLocal
#include "coverage/clip_coverage.inc"
#undef Rect
#undef HasClip
#undef LocalRect
#undef RadiiX
#undef RadiiY
#undef AntiAlias
#undef DeviceToLocal

void AnalyticClip_fragment(inout Fragment f) {
  f.coverage *= vec4(clipCoverage());
}
