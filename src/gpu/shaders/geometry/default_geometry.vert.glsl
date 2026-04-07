// Copyright (C) 2026 Tencent. All rights reserved.
// default_geometry.vert.glsl - DefaultGeometryProcessor vertex shader.
// Transforms position by a matrix and passes optional coverage to fragment shader.
//
// Attributes: position (vec2), coverage (float, optional)
// Uniforms: Matrix (mat3)
//
// Optional macros:
//   TGFX_GP_DEFAULT_COVERAGE_AA - defined if AA coverage attribute is present

// VS main logic:
// position attribute → transformed by Matrix → normalized position
// coverage attribute → passed as varying (if AA)

void TGFX_DefaultGP_VS(vec2 inPosition, mat3 matrix,
#ifdef TGFX_GP_DEFAULT_COVERAGE_AA
                        float inCoverage, out float vCoverage,
#endif
                        out vec2 position) {
    position = (matrix * vec3(inPosition, 1.0)).xy;
#ifdef TGFX_GP_DEFAULT_COVERAGE_AA
    vCoverage = inCoverage;
#endif
}
