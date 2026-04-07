// Copyright (C) 2026 Tencent. All rights reserved.
// atlas_text_geometry.frag.glsl - AtlasTextGeometryProcessor fragment shader.
// Samples atlas texture and outputs color/coverage based on alpha-only or color mode.
//
// The AA coverage varying is set first but then overwritten by atlas texture alpha.
// For alpha-only textures: coverage = texColor.a, color = uniform/varying color.
// For color textures: color = unpremultiplied texColor.rgb, coverage = texColor.a.
//
// Optional macros:
//   TGFX_GP_ATLAS_COVERAGE_AA - defined if AA coverage varying is present
//   TGFX_GP_ATLAS_COMMON_COLOR - defined if using uniform color instead of per-vertex color
//   TGFX_GP_ATLAS_ALPHA_ONLY - defined if atlas texture is alpha-only

void TGFX_AtlasTextGP_FS(vec2 vTextureCoords, sampler2D textureSampler,
#ifdef TGFX_GP_ATLAS_COVERAGE_AA
                          float vCoverage,
#endif
#ifdef TGFX_GP_ATLAS_COMMON_COLOR
                          vec4 uniformColor,
#else
                          vec4 vColor,
#endif
                          out vec4 outputColor, out vec4 outputCoverage) {
    // Initial coverage from AA (may be overwritten by atlas texture)
#ifdef TGFX_GP_ATLAS_COVERAGE_AA
    outputCoverage = vec4(vCoverage);
#else
    outputCoverage = vec4(1.0);
#endif

    // Set color from uniform or varying
#ifdef TGFX_GP_ATLAS_COMMON_COLOR
    outputColor = uniformColor;
#else
    outputColor = vColor;
#endif

    // Sample atlas texture
    vec4 texColor = texture(textureSampler, vTextureCoords);

#ifdef TGFX_GP_ATLAS_ALPHA_ONLY
    // Alpha-only: coverage comes from texture alpha, color stays as uniform/varying
    outputCoverage = vec4(texColor.a);
#else
    // Color texture: unpremultiply and use texture alpha as coverage
    outputColor = clamp(vec4(texColor.rgb / texColor.a, 1.0), 0.0, 1.0);
    outputCoverage = vec4(texColor.a);
#endif
}
