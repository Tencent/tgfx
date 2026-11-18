// Pure rounded-rect coverage fragment: the arc-box SDF with the AA/nonAA mix. Flat value
// arguments (no struct) so every backend — including Metal's stitchable graphs — can call it.
vec4 tgfxOpRRectCoverage(vec4 in0, vec4 rect, vec4 radiiX, vec4 radiiY, float antiAlias,
                         mat3 deviceToLocal, highp vec2 fragCoord) {
  return in0 * vec4(analyticRRectCoverageAt(rect, radiiX, radiiY, antiAlias, deviceToLocal,
                                             fragCoord));
}
