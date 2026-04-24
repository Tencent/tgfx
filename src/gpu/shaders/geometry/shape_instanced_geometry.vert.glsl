// Copyright (C) 2026 Tencent. All rights reserved.
// shape_instanced_geometry.vert.glsl - ShapeInstancedGeometryProcessor vertex shader.
// Transforms position by ViewMatrix + offset for device-space position (used to write
// gl_Position) and in parallel computes the local-space coordinate vLocal = uvMatrix * position
// as a varying, which is the uv input to FP coord transforms.
//
// Attributes: position (vec2), offset (vec2), coverage (float, optional),
//             instanceColor (vec4, optional)
// Uniforms: ViewMatrix (mat3), UVMatrix (mat3)
//
// Depends on: tgfx_vs_boilerplate.glsl (TGFX_NormalizePosition).

void TGFX_ShapeInstancedGP_VS(vec2 inPosition, vec2 inOffset,
                                mat3 viewMatrix, mat3 uvMatrix,
#ifdef TGFX_GP_SHAPE_COVERAGE_AA
                                float inCoverage, out float vCoverage,
#endif
#ifdef TGFX_GP_SHAPE_VERTEX_COLORS
                                vec4 inInstanceColor, out vec4 vInstanceColor,
#endif
                                out vec2 vLocal) {
    vec2 devicePosition = (viewMatrix * vec3(inPosition, 1.0)).xy + inOffset;
    vLocal = (uvMatrix * vec3(inPosition, 1.0)).xy;
#ifdef TGFX_GP_SHAPE_COVERAGE_AA
    vCoverage = inCoverage;
#endif
#ifdef TGFX_GP_SHAPE_VERTEX_COLORS
    vInstanceColor = inInstanceColor;
#endif
    gl_Position = TGFX_NormalizePosition(devicePosition);
}
