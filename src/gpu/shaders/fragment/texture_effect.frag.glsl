// Copyright (C) 2026 Tencent. All rights reserved.
// texture_effect.frag.glsl - TextureEffect modular shader function.
// Samples a texture with optional subset clamping, YUV conversion, and RGBAAA alpha channel.
//
// Required macros:
//   TGFX_TE_TEXTURE_MODE - 0=RGBA, 1=I420, 2=NV12
//
// Optional macros:
//   TGFX_TE_SUBSET          - defined if subset clamping is needed
//   TGFX_TE_STRICT_CONSTRAINT - defined if extra strict subset clamp is needed
//   TGFX_TE_ALPHA_ONLY      - defined if texture is alpha-only
//   TGFX_TE_RGBAAA          - defined if RGBAAA alpha channel layout is used
//   TGFX_TE_YUV_LIMITED_RANGE - defined if YUV uses limited range (16-235)

#ifndef TGFX_TE_TEXTURE_MODE
#define TGFX_TE_TEXTURE_MODE 0
#endif

vec2 TGFX_TE_ClampCoord(vec2 coord
#ifdef TGFX_TE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
#ifdef TGFX_TE_SUBSET
    , vec4 subset
#endif
) {
    vec2 result = coord;
#ifdef TGFX_TE_STRICT_CONSTRAINT
    result = clamp(result, extraSubset.xy, extraSubset.zw);
#endif
#ifdef TGFX_TE_SUBSET
    result = clamp(result, subset.xy, subset.zw);
#endif
    return result;
}

#if TGFX_TE_TEXTURE_MODE == 0

// RGBA path
vec4 TGFX_TextureEffect(vec4 inputColor, vec2 texCoord, sampler2D textureSampler
#ifdef TGFX_TE_SUBSET
    , vec4 subset
#endif
#ifdef TGFX_TE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
#ifdef TGFX_TE_RGBAAA
    , vec2 alphaStart
#endif
) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    vec4 color = texture(textureSampler, finalCoord);
#ifdef TGFX_TE_RGBAAA
    color = clamp(color, 0.0, 1.0);
    vec2 alphaVertexColor = finalCoord + alphaStart;
    vec4 alpha = texture(textureSampler, alphaVertexColor);
    alpha = clamp(alpha, 0.0, 1.0);
    color = vec4(color.rgb * alpha.r, alpha.r);
#endif
#ifdef TGFX_TE_ALPHA_ONLY
    return color.a * inputColor;
#else
    return color * color.a;
#endif
}

#elif TGFX_TE_TEXTURE_MODE == 1

// I420 YUV path - 3 separate planes (Y, U, V)
vec4 TGFX_TextureEffect(vec4 inputColor, vec2 texCoord,
    sampler2D samplerY, sampler2D samplerU, sampler2D samplerV,
    mat3 colorConversion
#ifdef TGFX_TE_SUBSET
    , vec4 subset
#endif
#ifdef TGFX_TE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
#ifdef TGFX_TE_RGBAAA
    , vec2 alphaStart
#endif
) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    vec3 yuv;
    yuv.x = texture(samplerY, finalCoord).r;
    finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    yuv.y = texture(samplerU, finalCoord).r;
    finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    yuv.z = texture(samplerV, finalCoord).r;
#ifdef TGFX_TE_YUV_LIMITED_RANGE
    yuv.x -= (16.0 / 255.0);
#endif
    yuv.yz -= vec2(0.5, 0.5);
    vec3 rgb = clamp(colorConversion * yuv, 0.0, 1.0);
#ifdef TGFX_TE_RGBAAA
    vec2 alphaVertexColor = finalCoord + alphaStart;
    float yuv_a = texture(samplerY, alphaVertexColor).r;
    yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
    yuv_a = clamp(yuv_a, 0.0, 1.0);
    return vec4(rgb * yuv_a, yuv_a);
#else
    return vec4(rgb, 1.0);
#endif
}

#elif TGFX_TE_TEXTURE_MODE == 2

// NV12 YUV path - 2 planes (Y, UV interleaved)
vec4 TGFX_TextureEffect(vec4 inputColor, vec2 texCoord,
    sampler2D samplerY, sampler2D samplerUV,
    mat3 colorConversion
#ifdef TGFX_TE_SUBSET
    , vec4 subset
#endif
#ifdef TGFX_TE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
#ifdef TGFX_TE_RGBAAA
    , vec2 alphaStart
#endif
) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    vec3 yuv;
    yuv.x = texture(samplerY, finalCoord).r;
    finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    yuv.yz = texture(samplerUV, finalCoord).ra;
#ifdef TGFX_TE_YUV_LIMITED_RANGE
    yuv.x -= (16.0 / 255.0);
#endif
    yuv.yz -= vec2(0.5, 0.5);
    vec3 rgb = clamp(colorConversion * yuv, 0.0, 1.0);
#ifdef TGFX_TE_RGBAAA
    vec2 alphaVertexColor = finalCoord + alphaStart;
    float yuv_a = texture(samplerY, alphaVertexColor).r;
    yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
    yuv_a = clamp(yuv_a, 0.0, 1.0);
    return vec4(rgb * yuv_a, yuv_a);
#else
    return vec4(rgb, 1.0);
#endif
}

#endif
