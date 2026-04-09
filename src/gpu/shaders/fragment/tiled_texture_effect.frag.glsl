// Copyright (C) 2026 Tencent. All rights reserved.
// tiled_texture_effect.frag.glsl - TiledTextureEffect modular shader function.
// Handles tiled/repeated/mirrored/border-clamped texture sampling with per-axis modes.
//
// Required macros:
//   TGFX_TTE_MODE_X - ShaderMode for X axis (0-8)
//   TGFX_TTE_MODE_Y - ShaderMode for Y axis (0-8)
//
// ShaderMode values:
//   0 = None
//   1 = Clamp
//   2 = RepeatNearestNone
//   3 = RepeatLinearNone
//   4 = RepeatLinearMipmap
//   5 = RepeatNearestMipmap
//   6 = MirrorRepeat
//   7 = ClampToBorderNearest
//   8 = ClampToBorderLinear
//
// Optional macros:
//   TGFX_TTE_STRICT_CONSTRAINT - defined if extra strict subset clamping is needed
//   TGFX_TTE_ALPHA_ONLY        - defined if texture is alpha-only
//   TGFX_TTE_HAS_DIMENSION     - defined if normalization dimension uniform is needed
//   TGFX_TTE_HAS_SUBSET        - defined if subset uniform is needed
//   TGFX_TTE_HAS_CLAMP         - defined if clamp uniform is needed

#ifndef TGFX_TTE_MODE_X
#define TGFX_TTE_MODE_X 0
#endif
#ifndef TGFX_TTE_MODE_Y
#define TGFX_TTE_MODE_Y 0
#endif

#ifndef TGFX_TTE_SAMPLER_TYPE
#define TGFX_TTE_SAMPLER_TYPE sampler2D
#endif

// Helper: does mode require unorm coord?
// Modes 3,4,5,8 require it
#define TGFX_TTE_UNORM_X (TGFX_TTE_MODE_X == 3 || TGFX_TTE_MODE_X == 4 || TGFX_TTE_MODE_X == 5 || TGFX_TTE_MODE_X == 8)
#define TGFX_TTE_UNORM_Y (TGFX_TTE_MODE_Y == 3 || TGFX_TTE_MODE_Y == 4 || TGFX_TTE_MODE_Y == 5 || TGFX_TTE_MODE_Y == 8)

// Helper: does mode use subset?
// All modes except 0 (None), 1 (Clamp), 8 (ClampToBorderLinear)
#define TGFX_TTE_USES_SUBSET_X (TGFX_TTE_MODE_X != 0 && TGFX_TTE_MODE_X != 1 && TGFX_TTE_MODE_X != 8)
#define TGFX_TTE_USES_SUBSET_Y (TGFX_TTE_MODE_Y != 0 && TGFX_TTE_MODE_Y != 1 && TGFX_TTE_MODE_Y != 8)

// Helper: does mode use clamp?
// All modes except 0 (None), 7 (ClampToBorderNearest)
#define TGFX_TTE_USES_CLAMP_X (TGFX_TTE_MODE_X != 0 && TGFX_TTE_MODE_X != 7)
#define TGFX_TTE_USES_CLAMP_Y (TGFX_TTE_MODE_Y != 0 && TGFX_TTE_MODE_Y != 7)

// Helper: is mipmap repeat mode?
#define TGFX_TTE_MIPMAP_X (TGFX_TTE_MODE_X == 4 || TGFX_TTE_MODE_X == 5)
#define TGFX_TTE_MIPMAP_Y (TGFX_TTE_MODE_Y == 4 || TGFX_TTE_MODE_Y == 5)

// Helper: is linear repeat mode (for error-based sub-texel blending)?
#define TGFX_TTE_LINEAR_REPEAT_X (TGFX_TTE_MODE_X == 3 || TGFX_TTE_MODE_X == 4)
#define TGFX_TTE_LINEAR_REPEAT_Y (TGFX_TTE_MODE_Y == 3 || TGFX_TTE_MODE_Y == 4)

vec4 TGFX_TiledTextureEffect(vec4 inputColor, vec2 texCoord, TGFX_TTE_SAMPLER_TYPE textureSampler
#ifdef TGFX_TTE_HAS_SUBSET
    , vec4 subset
#endif
#ifdef TGFX_TTE_HAS_CLAMP
    , vec4 clampRect
#endif
#ifdef TGFX_TTE_HAS_DIMENSION
    , vec2 dimension
#endif
#ifdef TGFX_TTE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
) {
    // Fast path: both modes are None
#if TGFX_TTE_MODE_X == 0 && TGFX_TTE_MODE_Y == 0
    vec4 result = texture(textureSampler, texCoord);
#else
    vec2 inCoord = texCoord;

#ifdef TGFX_TTE_HAS_DIMENSION
    inCoord /= dimension;
#endif

    // Mipmap repeat variables
#if TGFX_TTE_MIPMAP_X || TGFX_TTE_MIPMAP_Y
    vec2 extraRepeatCoord;
#endif
#if TGFX_TTE_MIPMAP_X
    float repeatCoordWeightX;
#endif
#if TGFX_TTE_MIPMAP_Y
    float repeatCoordWeightY;
#endif

    // --- Subset coordinate computation ---
    highp vec2 subsetCoord;

    // X axis subset coord
#if TGFX_TTE_MODE_X == 0 || TGFX_TTE_MODE_X == 7 || TGFX_TTE_MODE_X == 8 || TGFX_TTE_MODE_X == 1
    subsetCoord.x = inCoord.x;
#elif TGFX_TTE_MODE_X == 2 || TGFX_TTE_MODE_X == 3
    subsetCoord.x = mod(inCoord.x - subset.x, subset.z - subset.x) + subset.x;
#elif TGFX_TTE_MODE_X == 4 || TGFX_TTE_MODE_X == 5
    {
        float w = subset.z - subset.x;
        float w2 = 2.0 * w;
        float d = inCoord.x - subset.x;
        float m = mod(d, w2);
        float o = mix(m, w2 - m, step(w, m));
        subsetCoord.x = o + subset.x;
        extraRepeatCoord.x = w - o + subset.x;
        float hw = w / 2.0;
        float n = mod(d - hw, w2);
        repeatCoordWeightX = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);
    }
#elif TGFX_TTE_MODE_X == 6
    {
        float w = subset.z - subset.x;
        float w2 = 2.0 * w;
        float m = mod(inCoord.x - subset.x, w2);
        subsetCoord.x = mix(m, w2 - m, step(w, m)) + subset.x;
    }
#endif

    // Y axis subset coord
#if TGFX_TTE_MODE_Y == 0 || TGFX_TTE_MODE_Y == 7 || TGFX_TTE_MODE_Y == 8 || TGFX_TTE_MODE_Y == 1
    subsetCoord.y = inCoord.y;
#elif TGFX_TTE_MODE_Y == 2 || TGFX_TTE_MODE_Y == 3
    subsetCoord.y = mod(inCoord.y - subset.y, subset.w - subset.y) + subset.y;
#elif TGFX_TTE_MODE_Y == 4 || TGFX_TTE_MODE_Y == 5
    {
        float w = subset.w - subset.y;
        float w2 = 2.0 * w;
        float d = inCoord.y - subset.y;
        float m = mod(d, w2);
        float o = mix(m, w2 - m, step(w, m));
        subsetCoord.y = o + subset.y;
        extraRepeatCoord.y = w - o + subset.y;
        float hw = w / 2.0;
        float n = mod(d - hw, w2);
        repeatCoordWeightY = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);
    }
#elif TGFX_TTE_MODE_Y == 6
    {
        float w = subset.w - subset.y;
        float w2 = 2.0 * w;
        float m = mod(inCoord.y - subset.y, w2);
        subsetCoord.y = mix(m, w2 - m, step(w, m)) + subset.y;
    }
#endif

    // --- Clamp coordinate computation ---
    highp vec2 clampedCoord;

#if TGFX_TTE_USES_CLAMP_X && TGFX_TTE_USES_CLAMP_Y
    // Both axes use clamp
    clampedCoord = clamp(subsetCoord, clampRect.xy, clampRect.zw);
#elif TGFX_TTE_USES_CLAMP_X
    clampedCoord.x = clamp(subsetCoord.x, clampRect.x, clampRect.z);
    clampedCoord.y = subsetCoord.y;
#elif TGFX_TTE_USES_CLAMP_Y
    clampedCoord.x = subsetCoord.x;
    clampedCoord.y = clamp(subsetCoord.y, clampRect.y, clampRect.w);
#else
    clampedCoord = subsetCoord;
#endif

    // --- Strict constraint clamping ---
#ifdef TGFX_TTE_STRICT_CONSTRAINT
    {
#ifdef TGFX_TTE_HAS_DIMENSION
        highp vec4 normExtraSubset = extraSubset;
        normExtraSubset.xy /= dimension;
        normExtraSubset.zw /= dimension;
        clampedCoord = clamp(clampedCoord, normExtraSubset.xy, normExtraSubset.zw);
#else
        clampedCoord = clamp(clampedCoord, extraSubset.xy, extraSubset.zw);
#endif
    }
#endif

    // --- Mipmap repeat extra coord clamping ---
#if TGFX_TTE_MIPMAP_X && TGFX_TTE_MIPMAP_Y
    extraRepeatCoord = clamp(extraRepeatCoord, clampRect.xy, clampRect.zw);
#elif TGFX_TTE_MIPMAP_X
    extraRepeatCoord.x = clamp(extraRepeatCoord.x, clampRect.x, clampRect.z);
#elif TGFX_TTE_MIPMAP_Y
    extraRepeatCoord.y = clamp(extraRepeatCoord.y, clampRect.y, clampRect.w);
#endif

    // --- Texture read with mipmap blending ---
#if TGFX_TTE_MIPMAP_X && TGFX_TTE_MIPMAP_Y
#ifdef TGFX_TTE_HAS_DIMENSION
    vec4 textureColor1 = texture(textureSampler, clampedCoord * dimension);
    vec4 textureColor2 = texture(textureSampler, vec2(extraRepeatCoord.x, clampedCoord.y) * dimension);
    vec4 textureColor3 = texture(textureSampler, vec2(clampedCoord.x, extraRepeatCoord.y) * dimension);
    vec4 textureColor4 = texture(textureSampler, vec2(extraRepeatCoord.x, extraRepeatCoord.y) * dimension);
#else
    vec4 textureColor1 = texture(textureSampler, clampedCoord);
    vec4 textureColor2 = texture(textureSampler, vec2(extraRepeatCoord.x, clampedCoord.y));
    vec4 textureColor3 = texture(textureSampler, vec2(clampedCoord.x, extraRepeatCoord.y));
    vec4 textureColor4 = texture(textureSampler, vec2(extraRepeatCoord.x, extraRepeatCoord.y));
#endif
    vec4 textureColor = mix(mix(textureColor1, textureColor2, repeatCoordWeightX),
                            mix(textureColor3, textureColor4, repeatCoordWeightX), repeatCoordWeightY);
#elif TGFX_TTE_MIPMAP_X
#ifdef TGFX_TTE_HAS_DIMENSION
    vec4 textureColor1 = texture(textureSampler, clampedCoord * dimension);
    vec4 textureColor2 = texture(textureSampler, vec2(extraRepeatCoord.x, clampedCoord.y) * dimension);
#else
    vec4 textureColor1 = texture(textureSampler, clampedCoord);
    vec4 textureColor2 = texture(textureSampler, vec2(extraRepeatCoord.x, clampedCoord.y));
#endif
    vec4 textureColor = mix(textureColor1, textureColor2, repeatCoordWeightX);
#elif TGFX_TTE_MIPMAP_Y
#ifdef TGFX_TTE_HAS_DIMENSION
    vec4 textureColor1 = texture(textureSampler, clampedCoord * dimension);
    vec4 textureColor2 = texture(textureSampler, vec2(clampedCoord.x, extraRepeatCoord.y) * dimension);
#else
    vec4 textureColor1 = texture(textureSampler, clampedCoord);
    vec4 textureColor2 = texture(textureSampler, vec2(clampedCoord.x, extraRepeatCoord.y));
#endif
    vec4 textureColor = mix(textureColor1, textureColor2, repeatCoordWeightY);
#else
    // Standard read - normalize if dimension is available
#ifdef TGFX_TTE_HAS_DIMENSION
    vec4 textureColor = texture(textureSampler, clampedCoord * dimension);
#else
    vec4 textureColor = texture(textureSampler, clampedCoord);
#endif
#endif

    // --- Linear repeat error-based blending ---
#if TGFX_TTE_LINEAR_REPEAT_X || TGFX_TTE_MODE_X == 8
    float errX = subsetCoord.x - clampedCoord.x;
#if TGFX_TTE_LINEAR_REPEAT_X
    float repeatCoordX = errX > 0.0 ? clampRect.x : clampRect.z;
#endif
#endif

#if TGFX_TTE_LINEAR_REPEAT_Y || TGFX_TTE_MODE_Y == 8
    float errY = subsetCoord.y - clampedCoord.y;
#if TGFX_TTE_LINEAR_REPEAT_Y
    float repeatCoordY = errY > 0.0 ? clampRect.y : clampRect.w;
#endif
#endif

    // Handle RepeatLinear blending for both axes
#if TGFX_TTE_LINEAR_REPEAT_X && TGFX_TTE_LINEAR_REPEAT_Y
    {
#ifdef TGFX_TTE_HAS_DIMENSION
        vec4 repeatReadX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y) * dimension);
        vec4 repeatReadY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY) * dimension);
        vec4 repeatReadXY = texture(textureSampler, vec2(repeatCoordX, repeatCoordY) * dimension);
#else
        vec4 repeatReadX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y));
        vec4 repeatReadY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY));
        vec4 repeatReadXY = texture(textureSampler, vec2(repeatCoordX, repeatCoordY));
#endif
        if (errX != 0.0 && errY != 0.0) {
            errX = abs(errX);
            textureColor = mix(mix(textureColor, repeatReadX, errX),
                               mix(repeatReadY, repeatReadXY, errX), abs(errY));
        } else if (errX != 0.0) {
#ifdef TGFX_TTE_HAS_DIMENSION
            vec4 rX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y) * dimension);
#else
            vec4 rX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y));
#endif
            textureColor = mix(textureColor, rX, errX);
        } else if (errY != 0.0) {
#ifdef TGFX_TTE_HAS_DIMENSION
            vec4 rY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY) * dimension);
#else
            vec4 rY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY));
#endif
            textureColor = mix(textureColor, rY, errY);
        }
    }
#elif TGFX_TTE_LINEAR_REPEAT_X
    if (errX != 0.0) {
#ifdef TGFX_TTE_HAS_DIMENSION
        vec4 repeatReadX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y) * dimension);
#else
        vec4 repeatReadX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y));
#endif
        textureColor = mix(textureColor, repeatReadX, errX);
    }
#elif TGFX_TTE_LINEAR_REPEAT_Y
    if (errY != 0.0) {
#ifdef TGFX_TTE_HAS_DIMENSION
        vec4 repeatReadY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY) * dimension);
#else
        vec4 repeatReadY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY));
#endif
        textureColor = mix(textureColor, repeatReadY, errY);
    }
#endif

    // --- ClampToBorderLinear fade ---
#if TGFX_TTE_MODE_X == 8
    textureColor = mix(textureColor, vec4(0.0), min(abs(errX), 1.0));
#endif
#if TGFX_TTE_MODE_Y == 8
    textureColor = mix(textureColor, vec4(0.0), min(abs(errY), 1.0));
#endif

    // --- ClampToBorderNearest discard ---
#if TGFX_TTE_MODE_X == 7
    {
        float snappedX = floor(inCoord.x + 0.001) + 0.5;
        if (snappedX < subset.x || snappedX > subset.z) {
            textureColor = vec4(0.0);
        }
    }
#endif
#if TGFX_TTE_MODE_Y == 7
    {
        float snappedY = floor(inCoord.y + 0.001) + 0.5;
        if (snappedY < subset.y || snappedY > subset.w) {
            textureColor = vec4(0.0);
        }
    }
#endif

    vec4 result = textureColor;
#endif  // !(MODE_X == 0 && MODE_Y == 0)

    // --- Alpha-only / premultiply ---
#ifdef TGFX_TTE_ALPHA_ONLY
    // Alpha-only textures (R8 format) store alpha in the red channel.
    result = result.rrrr;
    return result.a * inputColor;
#else
    return result * inputColor.a;
#endif
}
