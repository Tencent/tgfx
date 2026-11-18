// Pure local-rect coverage fragment: maps the fragment position through the device-to-local
// matrix, then the shared separable half-pixel ramp. The entry supplies the coordinate.
vec4 tgfxOpLocalRectCoverage(vec4 in0, vec4 rect, mat3 deviceToLocal, highp vec2 fragCoord) {
  return in0 * vec4(analyticLocalRectCoverageAt(rect, deviceToLocal, fragCoord));
}
