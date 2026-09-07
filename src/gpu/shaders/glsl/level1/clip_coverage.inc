// Unified analytic clip coverage, selected by the HasClip runtime uniform:
//   0 = no clip; 1 = device-space AA rect (Rect, uploaded half a pixel outward);
//   2 = rounded rect (LocalRect/RadiiX/RadiiY/AntiAlias/DeviceToLocal);
//   3 = local-space AA rect (LocalRect/DeviceToLocal).
// The shared math lives in analytic_clip_math.inc (line-by-line translations of
// GLSLRectEffect / GLSLRRectEffect emitCode).
#include "analytic_clip_math.inc"

float clipCoverage() {
  if (HasClip == 2) {
    return analyticRRectCoverage(LocalRect, RadiiX, RadiiY, AntiAlias, DeviceToLocal);
  }
  if (HasClip == 3) {
    return analyticLocalRectCoverage(LocalRect, DeviceToLocal);
  }
  highp vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) *
                                 vec4(gl_FragCoord.xyxy - Rect),
                             0.0, 1.0);
  highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
  return HasClip != 0 ? dists2.x * dists2.y : 1.0;
}
