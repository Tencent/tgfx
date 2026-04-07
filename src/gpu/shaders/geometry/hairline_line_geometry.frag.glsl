// Copyright (C) 2026 Tencent. All rights reserved.
// hairline_line_geometry.frag.glsl - HairlineLineGeometryProcessor fragment shader.
// Computes edge distance coverage with AA step for hairline lines.
//
// Varyings: vEdgeDistance (float)
// Uniforms: Color (vec4), Coverage (float)
//
// Required macros:
//   (none)
// Optional macros:
//   TGFX_GP_HLINE_COVERAGE_AA - defined if AA coverage is used (smooth edge alpha)

void TGFX_HairlineLineGP_FS(float vEdgeDistance, vec4 color, float coverageScale,
                              out vec4 outputColor, out vec4 outputCoverage) {
    outputColor = color;
    float edgeAlpha = abs(vEdgeDistance);
    edgeAlpha = clamp(edgeAlpha, 0.0, 1.0);
#ifndef TGFX_GP_HLINE_COVERAGE_AA
    edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;
#endif
    outputCoverage = vec4(coverageScale * edgeAlpha);
}
