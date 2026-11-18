// Pure analytic AA-rect coverage fragment: the falloff from the fragment's distance to the rect
// edges. The entry supplies the fragment coordinate as a value.
vec4 tgfxOpAARectCoverage(vec4 in0, vec4 rect, highp vec2 fragCoord) {
  highp vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(fragCoord.xyxy - rect), 0.0, 1.0);
  highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
  return in0 * (dists2.x * dists2.y);
}
