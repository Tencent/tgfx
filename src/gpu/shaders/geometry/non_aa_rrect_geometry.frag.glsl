// Copyright (C) 2026 Tencent. All rights reserved.
// non_aa_rrect_geometry.frag.glsl - NonAARRectGeometryProcessor fragment shader.
// Computes SDF-based rounded rectangle coverage without anti-aliasing (step function).
// Supports optional stroke rendering with inner SDF subtraction.
//
// Optional macros:
//   TGFX_GP_NONAA_COMMON_COLOR - defined if using uniform color instead of per-vertex color
//   TGFX_GP_NONAA_STROKE - defined if rendering a stroked rounded rect

void TGFX_NonAARRectGP_FS(vec2 vLocalCoord, vec2 vRadii, vec4 vRectBounds,
#ifdef TGFX_GP_NONAA_COMMON_COLOR
                           vec4 uniformColor,
#else
                           vec4 vColor,
#endif
#ifdef TGFX_GP_NONAA_STROKE
                           vec2 vStrokeWidth,
#endif
                           out vec4 outputColor, out vec4 outputCoverage) {
#ifdef TGFX_GP_NONAA_COMMON_COLOR
    outputColor = uniformColor;
#else
    outputColor = vColor;
#endif

    vec2 localCoord = vLocalCoord;
    vec2 radii = vRadii;
    vec4 bounds = vRectBounds;
    vec2 center = (bounds.xy + bounds.zw) * 0.5;
    vec2 halfSize = (bounds.zw - bounds.xy) * 0.5;
    vec2 q = abs(localCoord - center) - halfSize + radii;
    float d = min(max(q.x / radii.x, q.y / radii.y), 0.0) + length(max(q / radii, 0.0)) - 1.0;
    float outerCoverage = step(d, 0.0);

#ifdef TGFX_GP_NONAA_STROKE
    vec2 sw = vStrokeWidth;
    vec2 innerHalfSize = halfSize - 2.0 * sw;
    vec2 innerRadii = max(radii - 2.0 * sw, vec2(0.0));
    float innerCoverage = 0.0;
    if (innerHalfSize.x > 0.0 && innerHalfSize.y > 0.0) {
        vec2 qi = abs(localCoord - center) - innerHalfSize + innerRadii;
        vec2 safeInnerRadii = max(innerRadii, vec2(0.001));
        float di = min(max(qi.x / safeInnerRadii.x, qi.y / safeInnerRadii.y), 0.0) +
                   length(max(qi / safeInnerRadii, vec2(0.0))) - 1.0;
        innerCoverage = step(di, 0.0);
    }
    float coverage = outerCoverage * (1.0 - innerCoverage);
#else
    float coverage = outerCoverage;
#endif

    outputCoverage = vec4(coverage);
}
