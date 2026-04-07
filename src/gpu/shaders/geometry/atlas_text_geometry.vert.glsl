// Copyright (C) 2026 Tencent. All rights reserved.
// atlas_text_geometry.vert.glsl - AtlasTextGeometryProcessor vertex shader.
// Transforms mask coordinates by atlas size inverse, passes coverage and optional color.
//
// Attributes: position (vec2), maskCoord (vec2), coverage (float, optional),
//             color (vec4, optional)
// Uniforms: atlasSizeInv (vec2)
//
// Optional macros:
//   TGFX_GP_ATLAS_COVERAGE_AA - defined if AA coverage attribute is present
//   TGFX_GP_ATLAS_COMMON_COLOR - defined if using uniform color instead of per-vertex color

void TGFX_AtlasTextGP_VS(vec2 inPosition, vec2 inMaskCoord, vec2 atlasSizeInv,
#ifdef TGFX_GP_ATLAS_COVERAGE_AA
                          float inCoverage, out float vCoverage,
#endif
#ifndef TGFX_GP_ATLAS_COMMON_COLOR
                          vec4 inColor, out vec4 vColor,
#endif
                          out vec2 vTextureCoords, out vec2 position) {
    vTextureCoords = inMaskCoord * atlasSizeInv;
#ifdef TGFX_GP_ATLAS_COVERAGE_AA
    vCoverage = inCoverage;
#endif
#ifndef TGFX_GP_ATLAS_COMMON_COLOR
    vColor = inColor;
#endif
    position = inPosition;
}
