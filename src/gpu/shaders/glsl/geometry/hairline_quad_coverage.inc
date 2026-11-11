// Hairline quad edge coverage, Loop-Blinn quadratic curve anti-aliasing: u^2 - v = 0 defines the
// curve. Both callers (the HairlineQuad family and the chain kernel) share this file so they stay
// byte-exact with each other.
float hairlineQuadEdgeCoverage(vec4 hairQuadEdge, float coverageScale, int aaEnabled) {
  highp vec2 duvdx = dFdx(hairQuadEdge.xy);
  highp vec2 duvdy = dFdy(hairQuadEdge.xy);
  highp vec2 gF = vec2(2.0 * hairQuadEdge.x * duvdx.x - duvdx.y,
                       2.0 * hairQuadEdge.x * duvdy.x - duvdy.y);
  highp float edgeAlpha = hairQuadEdge.x * hairQuadEdge.x - hairQuadEdge.y;
  edgeAlpha = sqrt(edgeAlpha * edgeAlpha / dot(gF, gF));
  edgeAlpha = max(1.0 - edgeAlpha, 0.0);
  if (aaEnabled == 0) {
    edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;
  }
  return coverageScale * edgeAlpha;
}
