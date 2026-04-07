// Copyright (C) 2026 Tencent. All rights reserved.
// hairline_quad_geometry.frag.glsl - HairlineQuadGeometryProcessor fragment shader.
// Evaluates Loop-Blinn quadratic curve coverage using dFdx/dFdy screen-space derivatives.
//
// Varyings: vHairQuadEdge (vec4)
// Uniforms: Color (vec4), Coverage (float)
//
// Required macros:
//   (none)
// Optional macros:
//   TGFX_GP_HQUAD_COVERAGE_AA - defined if AA coverage is used (smooth edge alpha)

void TGFX_HairlineQuadGP_FS(vec4 vHairQuadEdge, vec4 color, float coverageScale,
                              out vec4 outputColor, out vec4 outputCoverage) {
    outputColor = color;
    float edgeAlpha;
    vec2 duvdx = vec2(dFdx(vHairQuadEdge.xy));
    vec2 duvdy = vec2(dFdy(vHairQuadEdge.xy));
    vec2 gF = vec2(2.0 * vHairQuadEdge.x * duvdx.x - duvdx.y,
                   2.0 * vHairQuadEdge.x * duvdy.x - duvdy.y);
    edgeAlpha = float(vHairQuadEdge.x * vHairQuadEdge.x - vHairQuadEdge.y);
    edgeAlpha = sqrt(edgeAlpha * edgeAlpha / dot(gF, gF));
    edgeAlpha = max(1.0 - edgeAlpha, 0.0);
#ifndef TGFX_GP_HQUAD_COVERAGE_AA
    edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;
#endif
    outputCoverage = vec4(coverageScale * edgeAlpha);
}
