// Analytic AARect clip coverage. Rect is uploaded half a pixel outward by AARectEffect.
// HasClip is a three-state selector: 0 = no clip, 1 = device-space rect (below), 2 = rounded
// rect (rrectClipCoverage, whose LocalRect/RadiiX/RadiiY/DeviceToLocal data is written by
// GLSLRRectEffect::onSetData). The rounded-rect form is a uniform branch, not a permutation
// dimension, so it adds zero variants.
#include "rrect_clip_coverage.inc"

float aaRectClipCoverage() {
  if (HasClip == 2) {
    return rrectClipCoverage();
  }
  highp vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) *
                                 vec4(gl_FragCoord.xyxy - Rect),
                             0.0, 1.0);
  highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
  return HasClip != 0 ? dists2.x * dists2.y : 1.0;
}
