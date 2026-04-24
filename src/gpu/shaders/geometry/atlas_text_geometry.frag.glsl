// Copyright (C) 2026 Tencent. All rights reserved.
// atlas_text_geometry.frag.glsl - AtlasTextGeometryProcessor fragment shader helpers.
// Provides texture sampling functions for the glyph atlas, handling backend-specific sampler
// types (sampler2D / sampler2DRect) via GLSL function overloading. Channel swizzle (RRRR for
// alpha-only atlases, RGBA otherwise) is selected at compile time via TGFX_GP_ATLAS_ALPHA_ONLY.
//
// Optional macros:
//   TGFX_GP_ATLAS_ALPHA_ONLY - defined when the atlas texture is ALPHA_8 (glyph mask) format,
//                              in which case the single-channel alpha value must be replicated
//                              into all four channels so the downstream color/coverage path can
//                              treat it uniformly with RGBA atlases.

vec4 TGFX_AtlasText_SampleAtlas(sampler2D tex, vec2 uv) {
#ifdef TGFX_GP_ATLAS_ALPHA_ONLY
    return texture(tex, uv).rrrr;
#else
    return texture(tex, uv);
#endif
}

vec4 TGFX_AtlasText_SampleAtlas(sampler2DRect tex, vec2 uv) {
#ifdef TGFX_GP_ATLAS_ALPHA_ONLY
    return texture(tex, uv).rrrr;
#else
    return texture(tex, uv);
#endif
}
