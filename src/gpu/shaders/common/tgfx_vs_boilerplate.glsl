// Copyright (C) 2026 Tencent. All rights reserved.
// tgfx_vs_boilerplate.glsl - Shared vertex shader boilerplate helpers.
// All helpers operate on the global uniform `tgfx_RTAdjust` (declared by ModularProgramBuilder).
//
// tgfx_RTAdjust layout: vec4(scaleX, translateX, scaleY, translateY) — applied as:
//     gl_Position.xy = devPos.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw;
// This converts from device-space (pixel) coords to normalized device coords (NDC) while
// accounting for the render target's origin flipping convention.

/**
 * Converts a 2D device-space position to gl_Position using the RTAdjust uniform.
 * Equivalent to: gl_Position = vec4(devPos.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0, 1);
 */
vec4 TGFX_NormalizePosition(highp vec2 devPos) {
    return vec4(devPos.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}

/**
 * Transforms a 2D point by a mat3 (affine) and returns the resulting 2D position.
 * Used by GPs that need to transform auxiliary points (e.g. subset bounds).
 */
vec2 TGFX_TransformPoint(vec2 srcPoint, mat3 matrix) {
    return (matrix * vec3(srcPoint, 1.0)).xy;
}

/**
 * Transforms a 2D point by a mat3 (perspective-aware) and returns homogeneous-divided vec2.
 * Use this variant when the matrix may contain a perspective row.
 */
vec2 TGFX_TransformPointPersp(vec2 srcPoint, mat3 matrix) {
    vec3 p = matrix * vec3(srcPoint, 1.0);
    return p.xy / p.z;
}
