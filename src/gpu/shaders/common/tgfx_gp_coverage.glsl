// Copyright (C) 2026 Tencent. All rights reserved.
// tgfx_gp_coverage.glsl - Shared GeometryProcessor coverage helpers.
// These helpers compute anti-aliased coverage from per-vertex attributes in the fragment shader.

/**
 * Computes coverage for an ellipse using analytical gradient-based SDF.
 * Common path for EllipseGeometryProcessor and RoundStrokeRectGeometryProcessor's outer edge.
 *
 * @param offset   Normalized offset from ellipse center (in unit-circle space).
 * @param radii    Ellipse radii (vec2 for fill, xy=outer for stroke).
 * @return         Edge alpha in [0, 1].
 */
float TGFX_EllipseOuterCoverage(vec2 offset, vec2 radii) {
    float test = dot(offset, offset) - 1.0;
    vec2 grad = 2.0 * offset * radii;
    float grad_dot = max(dot(grad, grad), 1.1755e-38);
    float invlen = inversesqrt(grad_dot);
    return clamp(0.5 - test * invlen, 0.0, 1.0);
}

/**
 * Computes inner (subtractive) coverage for a stroked ellipse.
 * Used to carve out the inside of stroked ellipses.
 */
float TGFX_EllipseInnerCoverage(vec2 offset, vec2 innerRadii) {
    offset = offset * innerRadii;
    float test = dot(offset, offset) - 1.0;
    vec2 grad = 2.0 * offset * innerRadii;
    float grad_dot = max(dot(grad, grad), 1.1755e-38);
    float invlen = inversesqrt(grad_dot);
    return clamp(0.5 + test * invlen, 0.0, 1.0);
}

/**
 * Computes coverage for an axis-aligned hairline edge (AA line).
 *
 * @param edgeDist  Signed edge distance from the hairline.
 * @return          Alpha in [0, 1] — clamped absolute distance.
 */
float TGFX_HairlineLineCoverage(float edgeDist) {
    return clamp(abs(edgeDist), 0.0, 1.0);
}

/**
 * Computes coverage for a quadratic (curve) hairline edge using screen-space gradient.
 *
 * @param edge  Varying with edge.xy carrying curve parameters (u, v) where f = u^2 - v.
 * @return      Alpha in [0, 1].
 */
float TGFX_HairlineQuadCoverage(vec2 edge) {
    vec2 duvdx = dFdx(edge);
    vec2 duvdy = dFdy(edge);
    vec2 gF = vec2(2.0 * edge.x * duvdx.x - duvdx.y,
                   2.0 * edge.x * duvdy.x - duvdy.y);
    float f = edge.x * edge.x - edge.y;
    float edgeAlpha = sqrt(f * f / dot(gF, gF));
    return max(1.0 - edgeAlpha, 0.0);
}

/**
 * Computes coverage for an axis-aligned rounded-rect (outer edge).
 *
 * @param localCoord  Fragment's local-space coord.
 * @param bounds      Rect bounds as (left, top, right, bottom).
 * @param radii       Corner radii.
 * @return            Alpha: 1.0 inside, 0.0 outside.
 */
float TGFX_RRectOuterCoverage(vec2 localCoord, vec4 bounds, vec2 radii) {
    vec2 center = (bounds.xy + bounds.zw) * 0.5;
    vec2 halfSize = (bounds.zw - bounds.xy) * 0.5;
    vec2 q = abs(localCoord - center) - halfSize + radii;
    float d = min(max(q.x / radii.x, q.y / radii.y), 0.0) +
              length(max(q / radii, 0.0)) - 1.0;
    return step(d, 0.0);
}

/**
 * Computes coverage for the inner hole of a stroked rounded-rect.
 */
float TGFX_RRectInnerCoverage(vec2 localCoord, vec4 bounds, vec2 radii, vec2 strokeWidth) {
    vec2 center = (bounds.xy + bounds.zw) * 0.5;
    vec2 halfSize = (bounds.zw - bounds.xy) * 0.5;
    vec2 innerHalfSize = halfSize - 2.0 * strokeWidth;
    vec2 innerRadii = max(radii - 2.0 * strokeWidth, vec2(0.0));
    if (innerHalfSize.x <= 0.0 || innerHalfSize.y <= 0.0) {
        return 0.0;
    }
    vec2 qi = abs(localCoord - center) - innerHalfSize + innerRadii;
    vec2 safeInnerRadii = max(innerRadii, vec2(0.001));
    float di = min(max(qi.x / safeInnerRadii.x, qi.y / safeInnerRadii.y), 0.0) +
               length(max(qi / safeInnerRadii, vec2(0.0))) - 1.0;
    return step(di, 0.0);
}
