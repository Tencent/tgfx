// Copyright (C) 2026 Tencent. All rights reserved.
// texture_effect.frag.glsl - TextureEffect modular shader function.
// Completely macro-free. Uses GLSL function overloading for sampler2D/sampler2DRect.

vec2 TGFX_TE_ClampCoord(vec2 coord, vec4 extraSubset, vec4 subset,
                         int hasStrictConstraint, int hasSubset) {
    vec2 result = coord;
    if (hasStrictConstraint == 1) {
        result = clamp(result, extraSubset.xy, extraSubset.zw);
    }
    if (hasSubset == 1) {
        result = clamp(result, subset.xy, subset.zw);
    }
    return result;
}

// RGBA path - sampler2D
vec4 TGFX_TextureEffect_RGBA(vec4 inputColor, vec2 texCoord,
                              sampler2D textureSampler,
                              vec4 subset, vec4 extraSubset, vec2 alphaStart,
                              int hasSubset, int hasStrictConstraint,
                              int hasRGBAA, int alphaOnly) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord, extraSubset, subset,
                                          hasStrictConstraint, hasSubset);
    vec4 color = texture(textureSampler, finalCoord);
    if (alphaOnly == 1) {
        color = color.rrrr;
    }
    if (hasRGBAA == 1) {
        color = clamp(color, 0.0, 1.0);
        vec2 alphaVertexColor = finalCoord + alphaStart;
        vec4 alpha = texture(textureSampler, alphaVertexColor);
        alpha = clamp(alpha, 0.0, 1.0);
        color = vec4(color.rgb * alpha.r, alpha.r);
    }
    if (alphaOnly == 1) {
        return color.a * inputColor;
    } else {
        return color * inputColor.a;
    }
}

// RGBA path - sampler2DRect overload for macOS Rectangle textures
vec4 TGFX_TextureEffect_RGBA(vec4 inputColor, vec2 texCoord,
                              sampler2DRect textureSampler,
                              vec4 subset, vec4 extraSubset, vec2 alphaStart,
                              int hasSubset, int hasStrictConstraint,
                              int hasRGBAA, int alphaOnly) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord, extraSubset, subset,
                                          hasStrictConstraint, hasSubset);
    vec4 color = texture(textureSampler, finalCoord);
    if (alphaOnly == 1) {
        color = color.rrrr;
    }
    if (hasRGBAA == 1) {
        color = clamp(color, 0.0, 1.0);
        vec2 alphaVertexColor = finalCoord + alphaStart;
        vec4 alpha = texture(textureSampler, alphaVertexColor);
        alpha = clamp(alpha, 0.0, 1.0);
        color = vec4(color.rgb * alpha.r, alpha.r);
    }
    if (alphaOnly == 1) {
        return color.a * inputColor;
    } else {
        return color * inputColor.a;
    }
}

// I420 YUV path - 3 separate planes (Y, U, V)
vec4 TGFX_TextureEffect_I420(vec4 inputColor, vec2 texCoord,
                              sampler2D samplerY, sampler2D samplerU, sampler2D samplerV,
                              mat3 colorConversion,
                              vec4 subset, vec4 extraSubset, vec2 alphaStart,
                              int hasSubset, int hasStrictConstraint,
                              int hasRGBAA, int yuvLimitedRange) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord, extraSubset, subset,
                                          hasStrictConstraint, hasSubset);
    vec3 yuv;
    yuv.x = texture(samplerY, finalCoord).r;
    yuv.y = texture(samplerU, finalCoord).r;
    yuv.z = texture(samplerV, finalCoord).r;
    if (yuvLimitedRange == 1) {
        yuv.x -= (16.0 / 255.0);
    }
    yuv.yz -= vec2(0.5, 0.5);
    vec3 rgb = clamp(colorConversion * yuv, 0.0, 1.0);
    if (hasRGBAA == 1) {
        vec2 alphaVertexColor = finalCoord + alphaStart;
        float yuv_a = texture(samplerY, alphaVertexColor).r;
        yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
        yuv_a = clamp(yuv_a, 0.0, 1.0);
        return vec4(rgb * yuv_a, yuv_a);
    } else {
        return vec4(rgb, 1.0);
    }
}

// NV12 YUV path - 2 planes (Y, UV interleaved)
vec4 TGFX_TextureEffect_NV12(vec4 inputColor, vec2 texCoord,
                              sampler2D samplerY, sampler2D samplerUV,
                              mat3 colorConversion,
                              vec4 subset, vec4 extraSubset, vec2 alphaStart,
                              int hasSubset, int hasStrictConstraint,
                              int hasRGBAA, int yuvLimitedRange) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord, extraSubset, subset,
                                          hasStrictConstraint, hasSubset);
    vec3 yuv;
    yuv.x = texture(samplerY, finalCoord).r;
    yuv.yz = texture(samplerUV, finalCoord).ra;
    if (yuvLimitedRange == 1) {
        yuv.x -= (16.0 / 255.0);
    }
    yuv.yz -= vec2(0.5, 0.5);
    vec3 rgb = clamp(colorConversion * yuv, 0.0, 1.0);
    if (hasRGBAA == 1) {
        vec2 alphaVertexColor = finalCoord + alphaStart;
        float yuv_a = texture(samplerY, alphaVertexColor).r;
        yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
        yuv_a = clamp(yuv_a, 0.0, 1.0);
        return vec4(rgb * yuv_a, yuv_a);
    } else {
        return vec4(rgb, 1.0);
    }
}
