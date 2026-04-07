// Copyright (C) 2026 Tencent. All rights reserved.
// shape_instanced_geometry.vert.glsl - ShapeInstancedGeometryProcessor vertex shader.
// Transforms position by UVMatrix for local coords, by ViewMatrix + offset for screen position.
// Passes optional per-instance color and coverage varyings.
//
// Attributes: position (vec2), offset (vec2), coverage (float, optional),
//             instanceColor (vec4, optional)
// Uniforms: UVMatrix (mat3), ViewMatrix (mat3)

void TGFX_ShapeInstancedGP_VS(vec2 inPosition, vec2 inOffset,
                                mat3 uvMatrix, mat3 viewMatrix,
#ifdef TGFX_GP_SHAPE_COVERAGE_AA
                                float inCoverage, out float vCoverage,
#endif
#ifdef TGFX_GP_SHAPE_HAS_COLORS
                                vec4 inInstanceColor, out vec4 vInstanceColor,
#endif
                                out vec2 position) {
    highp vec2 local = (uvMatrix * vec3(inPosition, 1.0)).xy;
    position = (viewMatrix * vec3(inPosition, 1.0)).xy + inOffset;
#ifdef TGFX_GP_SHAPE_COVERAGE_AA
    vCoverage = inCoverage;
#endif
#ifdef TGFX_GP_SHAPE_HAS_COLORS
    vInstanceColor = inInstanceColor;
#endif
}
