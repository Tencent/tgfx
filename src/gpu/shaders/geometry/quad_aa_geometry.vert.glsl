// Copyright (C) 2026 Tencent. All rights reserved.
// quad_aa_geometry.vert.glsl - QuadPerEdgeAAGeometryProcessor vertex shader.
// Passes position directly (pre-transformed), with optional coverage and color varyings, and
// an optional subset rect computed in target space when TGFX_GP_QUAD_SUBSET is defined.
//
// Attributes: position (vec2), uvCoord (vec2, optional), coverage (float, optional),
//             color (vec4, optional), subset (vec4, optional)
//
// Optional macros:
//   TGFX_GP_QUAD_COVERAGE_AA - defined if AA coverage attribute is present
//   TGFX_GP_QUAD_COMMON_COLOR - defined if using uniform color instead of per-vertex color
//   TGFX_GP_QUAD_SUBSET - defined if this draw carries a texture subset rect
//
// Depends on: tgfx_vs_boilerplate.glsl (TGFX_TransformPointPersp is used for the subset even
//             when the matrix is affine; the extra divide-by-1 is negligible on modern GPUs
//             and lets us drop a perspective-vs-affine variant axis).

void TGFX_QuadAAGP_VS(vec2 inPosition,
#ifdef TGFX_GP_QUAD_COVERAGE_AA
                       float inCoverage, out float vCoverage,
#endif
#ifndef TGFX_GP_QUAD_COMMON_COLOR
                       vec4 inColor, out vec4 vColor,
#endif
#ifdef TGFX_GP_QUAD_SUBSET
                       vec4 inSubset, mat3 subsetMatrix, out vec4 vSubset,
#endif
                       out vec2 position) {
    position = inPosition;
#ifdef TGFX_GP_QUAD_COVERAGE_AA
    vCoverage = inCoverage;
#endif
#ifndef TGFX_GP_QUAD_COMMON_COLOR
    vColor = inColor;
#endif
#ifdef TGFX_GP_QUAD_SUBSET
    // Transform the src-space subset corners into target space, then sort into
    // axis-aligned vec4(minX, minY, maxX, maxY). Use the perspective helper unconditionally
    // so this file has zero perspective/affine branching — for affine matrices the helper
    // degenerates to a divide-by-1.0 which is a free op on any modern shader compiler path.
    vec2 dstLT = TGFX_TransformPointPersp(inSubset.xy, subsetMatrix);
    vec2 dstRB = TGFX_TransformPointPersp(inSubset.zw, subsetMatrix);
    vec4 r = vec4(dstLT, dstRB);
    if (r.x > r.z) {
        float t = r.x;
        r.x = r.z;
        r.z = t;
    }
    if (r.y > r.w) {
        float t = r.y;
        r.y = r.w;
        r.w = t;
    }
    vSubset = r;
#endif
}
