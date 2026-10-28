// Complex non-AA rounded-rect (per-corner radii) edge coverage, shared by
// ComplexNonAARRectFillShader's fill and stroke variants. The math is verbatim from the family's
// geometry emission (GLSLComplexNonAARRectGeometryProcessor::emitCode): the corner quadrant selects
// one arc center through the prefix-sum trick, the normalized-space inside-ellipse test is the outer
// coverage, and for strokes the inner arc and inner rect tests are subtracted from it. Every caller
// passes a compile-time constant for stroke, so the unused branch folds away; strokeWidth is only
// read when stroke is set.
float complexRRectCoverage(vec2 localCoord, vec4 rectBounds, vec4 xRadii, vec4 yRadii,
                           vec2 strokeWidth, bool stroke) {
  // Per-corner radii: xR/yR order is [TL, TR, BR, BL]
  highp vec4 cornersX = vec4(rectBounds.x, rectBounds.z, rectBounds.z, rectBounds.x);
  highp vec4 cornersY = vec4(rectBounds.y, rectBounds.y, rectBounds.w, rectBounds.w);
  highp vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);
  highp vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);
  highp vec4 dx = (vec4(localCoord.x) - cornersX) * signsX;
  highp vec4 dy = (vec4(localCoord.y) - cornersY) * signsY;
  highp vec4 inBox = step(dx, xRadii) * step(dy, yRadii);
  highp vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, inBox.x + inBox.y + inBox.z);
  inBox *= step(prefixSum, vec4(0.5));
  highp float inAnyCorner = dot(inBox, vec4(1.0));

  highp vec4 arcCentersX = cornersX + signsX * xRadii;
  highp vec4 arcCentersY = cornersY + signsY * yRadii;
  highp vec2 arcCenter = vec2(dot(inBox, arcCentersX), dot(inBox, arcCentersY));
  highp vec2 radii = max(vec2(dot(inBox, xRadii), dot(inBox, yRadii)), vec2(1e-6));

  highp vec2 ellipseOffset = (localCoord - arcCenter) / radii;
  highp float insideEllipse = step(dot(ellipseOffset, ellipseOffset), 1.0);
  highp float outerCoverage = mix(1.0, insideEllipse, inAnyCorner);

  if (!stroke) {
    return outerCoverage;
  }

  highp vec2 sw = strokeWidth;
  highp vec2 innerRadii = max(radii - 2.0 * sw, vec2(1e-6));
  highp vec2 innerEllipseOffset = (localCoord - arcCenter) / innerRadii;
  highp float insideInnerEllipse = step(dot(innerEllipseOffset, innerEllipseOffset), 1.0);
  highp vec2 center = (rectBounds.xy + rectBounds.zw) * 0.5;
  highp vec2 halfSize = (rectBounds.zw - rectBounds.xy) * 0.5;
  highp vec2 innerHalfSize = halfSize - 2.0 * sw;
  highp vec2 p = abs(localCoord - center);
  highp float insideInnerRect = step(0.0, innerHalfSize.x) * step(0.0, innerHalfSize.y)
                              * step(p.x, innerHalfSize.x) * step(p.y, innerHalfSize.y);
  highp float innerCoverage = mix(insideInnerRect, insideInnerEllipse, inAnyCorner);
  return outerCoverage * (1.0 - innerCoverage);
}
