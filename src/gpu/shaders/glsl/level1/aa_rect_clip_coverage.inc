// Analytic AARect clip coverage. Rect is uploaded half a pixel outward by AARectEffect.
float aaRectClipCoverage() {
  highp vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) *
                                 vec4(gl_FragCoord.xyxy - Rect),
                             0.0, 1.0);
  highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
  return HasClip != 0 ? dists2.x * dists2.y : 1.0;
}
