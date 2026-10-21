// Non-AA rounded-rect (uniform radii) edge coverage, shared by NonAARRectFillShader's fill and
// stroke variants. The math is verbatim from the family's geometry emission
// (GLSLNonAARRectGeometryProcessor::emitCode): a rounded-rect SDF per pixel, a hard step for the
// outer edge, and for strokes the inner edge SDF subtracted from it. Every caller passes a
// compile-time constant for stroke, so the unused branch folds away; strokeWidth is only read when
// stroke is set.
float nonAARRectCoverage(vec2 localCoord, vec4 rectBounds, vec2 radii, vec2 strokeWidth,
                         bool stroke) {
  highp vec2 center = (rectBounds.xy + rectBounds.zw) * 0.5;
  highp vec2 halfSize = (rectBounds.zw - rectBounds.xy) * 0.5;
  highp vec2 q = abs(localCoord - center) - halfSize + radii;
  highp float d = min(max(q.x / radii.x, q.y / radii.y), 0.0) +
                  length(max(q / radii, vec2(0.0))) - 1.0;
  highp float outerCoverage = step(d, 0.0);

  if (!stroke) {
    return outerCoverage;
  }

  // Inner round rect SDF for stroke
  highp vec2 innerHalfSize = halfSize - 2.0 * strokeWidth;
  highp vec2 innerRadii = max(radii - 2.0 * strokeWidth, vec2(0.0));
  highp float innerCoverage = 0.0;
  if (innerHalfSize.x > 0.0 && innerHalfSize.y > 0.0) {
    highp vec2 qi = abs(localCoord - center) - innerHalfSize + innerRadii;
    highp vec2 safeInnerRadii = max(innerRadii, vec2(0.001));
    highp float di = min(max(qi.x / safeInnerRadii.x, qi.y / safeInnerRadii.y), 0.0) +
                     length(max(qi / safeInnerRadii, vec2(0.0))) - 1.0;
    innerCoverage = step(di, 0.0);
  }
  return outerCoverage * (1.0 - innerCoverage);
}
