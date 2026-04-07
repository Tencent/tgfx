// Copyright (C) 2026 Tencent. All rights reserved.
// default_geometry.frag.glsl - DefaultGeometryProcessor fragment shader.
// Outputs uniform color and optional AA coverage.
//
// Uniforms: Color (vec4)
//
// Optional macros:
//   TGFX_GP_DEFAULT_COVERAGE_AA - defined if AA coverage varying is present

void TGFX_DefaultGP_FS(vec4 color,
#ifdef TGFX_GP_DEFAULT_COVERAGE_AA
                        float vCoverage,
#endif
                        out vec4 outputColor, out vec4 outputCoverage) {
    outputColor = color;
#ifdef TGFX_GP_DEFAULT_COVERAGE_AA
    outputCoverage = vec4(vCoverage);
#else
    outputCoverage = vec4(1.0);
#endif
}
