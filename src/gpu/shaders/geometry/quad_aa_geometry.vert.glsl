// Copyright (C) 2026 Tencent. All rights reserved.
// quad_aa_geometry.vert.glsl - QuadPerEdgeAAGeometryProcessor vertex shader.
// Passes position directly (pre-transformed), with optional coverage and color varyings.
//
// Attributes: position (vec2), uvCoord (vec2, optional), coverage (float, optional),
//             color (vec4, optional)
//
// Optional macros:
//   TGFX_GP_QUAD_COVERAGE_AA - defined if AA coverage attribute is present
//   TGFX_GP_QUAD_COMMON_COLOR - defined if using uniform color instead of per-vertex color
//
// Depends on: tgfx_vs_boilerplate.glsl (TGFX_TransformPoint / TGFX_TransformPointPersp)
//             when subset computation is used.

void TGFX_QuadAAGP_VS(vec2 inPosition,
#ifdef TGFX_GP_QUAD_COVERAGE_AA
                       float inCoverage, out float vCoverage,
#endif
#ifndef TGFX_GP_QUAD_COMMON_COLOR
                       vec4 inColor, out vec4 vColor,
#endif
                       out vec2 position) {
    position = inPosition;
#ifdef TGFX_GP_QUAD_COVERAGE_AA
    vCoverage = inCoverage;
#endif
#ifndef TGFX_GP_QUAD_COMMON_COLOR
    vColor = inColor;
#endif
}

// Computes the texture subset in target space given a pre-packed subset vec4 (srcLT.xy, srcRB.zw)
// and an affine transform matrix. Returns an axis-aligned vec4(minX, minY, maxX, maxY) with the
// bounds sorted so that xy <= zw element-wise, matching what the FS subset constraint expects.
vec4 TGFX_QuadAA_ComputeSubset(vec4 srcSubset, mat3 matrix) {
    vec2 srcLT = srcSubset.xy;
    vec2 srcRB = srcSubset.zw;
    vec2 dstLT = TGFX_TransformPoint(srcLT, matrix);
    vec2 dstRB = TGFX_TransformPoint(srcRB, matrix);
    vec4 result = vec4(dstLT, dstRB);
    if (result.x > result.z) {
        float tmp = result.x;
        result.x = result.z;
        result.z = tmp;
    }
    if (result.y > result.w) {
        float tmp = result.y;
        result.y = result.w;
        result.w = tmp;
    }
    return result;
}

// Perspective variant used when the subset transform matrix may contain a perspective row.
vec4 TGFX_QuadAA_ComputeSubsetPersp(vec4 srcSubset, mat3 matrix) {
    vec2 srcLT = srcSubset.xy;
    vec2 srcRB = srcSubset.zw;
    vec2 dstLT = TGFX_TransformPointPersp(srcLT, matrix);
    vec2 dstRB = TGFX_TransformPointPersp(srcRB, matrix);
    vec4 result = vec4(dstLT, dstRB);
    if (result.x > result.z) {
        float tmp = result.x;
        result.x = result.z;
        result.z = tmp;
    }
    if (result.y > result.w) {
        float tmp = result.y;
        result.y = result.w;
        result.w = tmp;
    }
    return result;
}
