// Copyright (C) 2026 Tencent. All rights reserved.
// tiled_texture_effect.frag.glsl - TiledTextureEffect modular shader function.
// Handles tiled/repeated/mirrored/border-clamped texture sampling with per-axis modes.
//
// Macro-free design: all configuration is passed as runtime function parameters.
// Uses GLSL function overloading for sampler2D vs sampler2DRect (macOS Rectangle textures).
//
// ShaderMode values (modeX/modeY):
//   0 = None
//   1 = Clamp
//   2 = RepeatNearestNone
//   3 = RepeatLinearNone
//   4 = RepeatLinearMipmap
//   5 = RepeatNearestMipmap
//   6 = MirrorRepeat
//   7 = ClampToBorderNearest
//   8 = ClampToBorderLinear

vec4 TGFX_TiledTextureEffect(vec4 inputColor, vec2 texCoord, sampler2D textureSampler,
                              vec4 subset, vec4 clampRect, vec2 dimension, vec4 extraSubset,
                              int modeX, int modeY, int alphaOnly,
                              int hasDimension, int hasSubset, int hasClamp,
                              int hasStrictConstraint) {
    // Derived boolean flags from mode parameters
    bool unormX = (modeX == 3 || modeX == 4 || modeX == 5 || modeX == 8);
    bool unormY = (modeY == 3 || modeY == 4 || modeY == 5 || modeY == 8);
    bool usesSubsetX = (modeX != 0 && modeX != 1 && modeX != 8);
    bool usesSubsetY = (modeY != 0 && modeY != 1 && modeY != 8);
    bool usesClampX = (modeX != 0 && modeX != 7);
    bool usesClampY = (modeY != 0 && modeY != 7);
    bool mipmapX = (modeX == 4 || modeX == 5);
    bool mipmapY = (modeY == 4 || modeY == 5);
    bool linearRepeatX = (modeX == 3 || modeX == 4);
    bool linearRepeatY = (modeY == 3 || modeY == 4);

    // Fast path: both modes are None
    if (modeX == 0 && modeY == 0) {
        vec4 result = texture(textureSampler, texCoord);
        if (alphaOnly == 1) {
            result = result.rrrr;
            return result.a * inputColor;
        } else {
            return result * inputColor.a;
        }
    }

    vec2 inCoord = texCoord;

    if (hasDimension == 1) {
        inCoord /= dimension;
    }

    // Mipmap repeat variables
    vec2 extraRepeatCoord = vec2(0.0);
    float repeatCoordWeightX = 0.0;
    float repeatCoordWeightY = 0.0;

    // --- Subset coordinate computation ---
    highp vec2 subsetCoord;

    // X axis subset coord
    if (modeX == 0 || modeX == 7 || modeX == 8 || modeX == 1) {
        subsetCoord.x = inCoord.x;
    } else if (modeX == 2 || modeX == 3) {
        subsetCoord.x = mod(inCoord.x - subset.x, subset.z - subset.x) + subset.x;
    } else if (modeX == 4 || modeX == 5) {
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
    } else if (modeX == 6) {
        float w = subset.z - subset.x;
        float w2 = 2.0 * w;
        float m = mod(inCoord.x - subset.x, w2);
        subsetCoord.x = mix(m, w2 - m, step(w, m)) + subset.x;
    }

    // Y axis subset coord
    if (modeY == 0 || modeY == 7 || modeY == 8 || modeY == 1) {
        subsetCoord.y = inCoord.y;
    } else if (modeY == 2 || modeY == 3) {
        subsetCoord.y = mod(inCoord.y - subset.y, subset.w - subset.y) + subset.y;
    } else if (modeY == 4 || modeY == 5) {
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
    } else if (modeY == 6) {
        float w = subset.w - subset.y;
        float w2 = 2.0 * w;
        float m = mod(inCoord.y - subset.y, w2);
        subsetCoord.y = mix(m, w2 - m, step(w, m)) + subset.y;
    }

    // --- Clamp coordinate computation ---
    highp vec2 clampedCoord;

    if (usesClampX && usesClampY) {
        clampedCoord = clamp(subsetCoord, clampRect.xy, clampRect.zw);
    } else if (usesClampX) {
        clampedCoord.x = clamp(subsetCoord.x, clampRect.x, clampRect.z);
        clampedCoord.y = subsetCoord.y;
    } else if (usesClampY) {
        clampedCoord.x = subsetCoord.x;
        clampedCoord.y = clamp(subsetCoord.y, clampRect.y, clampRect.w);
    } else {
        clampedCoord = subsetCoord;
    }

    // --- Strict constraint clamping ---
    if (hasStrictConstraint == 1) {
        if (hasDimension == 1) {
            highp vec4 normExtraSubset = extraSubset;
            normExtraSubset.xy /= dimension;
            normExtraSubset.zw /= dimension;
            clampedCoord = clamp(clampedCoord, normExtraSubset.xy, normExtraSubset.zw);
        } else {
            clampedCoord = clamp(clampedCoord, extraSubset.xy, extraSubset.zw);
        }
    }

    // --- Mipmap repeat extra coord clamping ---
    if (mipmapX && mipmapY) {
        extraRepeatCoord = clamp(extraRepeatCoord, clampRect.xy, clampRect.zw);
    } else if (mipmapX) {
        extraRepeatCoord.x = clamp(extraRepeatCoord.x, clampRect.x, clampRect.z);
    } else if (mipmapY) {
        extraRepeatCoord.y = clamp(extraRepeatCoord.y, clampRect.y, clampRect.w);
    }

    // --- Texture read with mipmap blending ---
    vec4 textureColor;

    if (mipmapX && mipmapY) {
        vec2 sampCoord1 = clampedCoord;
        vec2 sampCoord2 = vec2(extraRepeatCoord.x, clampedCoord.y);
        vec2 sampCoord3 = vec2(clampedCoord.x, extraRepeatCoord.y);
        vec2 sampCoord4 = vec2(extraRepeatCoord.x, extraRepeatCoord.y);
        if (hasDimension == 1) {
            sampCoord1 *= dimension;
            sampCoord2 *= dimension;
            sampCoord3 *= dimension;
            sampCoord4 *= dimension;
        }
        vec4 textureColor1 = texture(textureSampler, sampCoord1);
        vec4 textureColor2 = texture(textureSampler, sampCoord2);
        vec4 textureColor3 = texture(textureSampler, sampCoord3);
        vec4 textureColor4 = texture(textureSampler, sampCoord4);
        textureColor = mix(mix(textureColor1, textureColor2, repeatCoordWeightX),
                           mix(textureColor3, textureColor4, repeatCoordWeightX), repeatCoordWeightY);
    } else if (mipmapX) {
        vec2 sampCoord1 = clampedCoord;
        vec2 sampCoord2 = vec2(extraRepeatCoord.x, clampedCoord.y);
        if (hasDimension == 1) {
            sampCoord1 *= dimension;
            sampCoord2 *= dimension;
        }
        vec4 textureColor1 = texture(textureSampler, sampCoord1);
        vec4 textureColor2 = texture(textureSampler, sampCoord2);
        textureColor = mix(textureColor1, textureColor2, repeatCoordWeightX);
    } else if (mipmapY) {
        vec2 sampCoord1 = clampedCoord;
        vec2 sampCoord2 = vec2(clampedCoord.x, extraRepeatCoord.y);
        if (hasDimension == 1) {
            sampCoord1 *= dimension;
            sampCoord2 *= dimension;
        }
        vec4 textureColor1 = texture(textureSampler, sampCoord1);
        vec4 textureColor2 = texture(textureSampler, sampCoord2);
        textureColor = mix(textureColor1, textureColor2, repeatCoordWeightY);
    } else {
        // Standard read
        vec2 sampCoord = clampedCoord;
        if (hasDimension == 1) {
            sampCoord *= dimension;
        }
        textureColor = texture(textureSampler, sampCoord);
    }

    // --- Linear repeat error-based blending ---
    float errX = 0.0;
    float errY = 0.0;
    float repeatCoordX = 0.0;
    float repeatCoordY = 0.0;

    if (linearRepeatX || modeX == 8) {
        errX = subsetCoord.x - clampedCoord.x;
        if (linearRepeatX) {
            repeatCoordX = errX > 0.0 ? clampRect.x : clampRect.z;
        }
    }

    if (linearRepeatY || modeY == 8) {
        errY = subsetCoord.y - clampedCoord.y;
        if (linearRepeatY) {
            repeatCoordY = errY > 0.0 ? clampRect.y : clampRect.w;
        }
    }

    // Handle RepeatLinear blending for both axes
    if (linearRepeatX && linearRepeatY) {
        vec2 rCoordX = vec2(repeatCoordX, clampedCoord.y);
        vec2 rCoordY = vec2(clampedCoord.x, repeatCoordY);
        vec2 rCoordXY = vec2(repeatCoordX, repeatCoordY);
        if (hasDimension == 1) {
            rCoordX *= dimension;
            rCoordY *= dimension;
            rCoordXY *= dimension;
        }
        vec4 repeatReadX = texture(textureSampler, rCoordX);
        vec4 repeatReadY = texture(textureSampler, rCoordY);
        vec4 repeatReadXY = texture(textureSampler, rCoordXY);
        if (errX != 0.0 && errY != 0.0) {
            errX = abs(errX);
            textureColor = mix(mix(textureColor, repeatReadX, errX),
                               mix(repeatReadY, repeatReadXY, errX), abs(errY));
        } else if (errX != 0.0) {
            vec2 rxCoord = vec2(repeatCoordX, clampedCoord.y);
            if (hasDimension == 1) {
                rxCoord *= dimension;
            }
            vec4 rX = texture(textureSampler, rxCoord);
            textureColor = mix(textureColor, rX, errX);
        } else if (errY != 0.0) {
            vec2 ryCoord = vec2(clampedCoord.x, repeatCoordY);
            if (hasDimension == 1) {
                ryCoord *= dimension;
            }
            vec4 rY = texture(textureSampler, ryCoord);
            textureColor = mix(textureColor, rY, errY);
        }
    } else if (linearRepeatX) {
        if (errX != 0.0) {
            vec2 rxCoord = vec2(repeatCoordX, clampedCoord.y);
            if (hasDimension == 1) {
                rxCoord *= dimension;
            }
            vec4 repeatReadX = texture(textureSampler, rxCoord);
            textureColor = mix(textureColor, repeatReadX, errX);
        }
    } else if (linearRepeatY) {
        if (errY != 0.0) {
            vec2 ryCoord = vec2(clampedCoord.x, repeatCoordY);
            if (hasDimension == 1) {
                ryCoord *= dimension;
            }
            vec4 repeatReadY = texture(textureSampler, ryCoord);
            textureColor = mix(textureColor, repeatReadY, errY);
        }
    }

    // --- ClampToBorderLinear fade ---
    if (modeX == 8) {
        textureColor = mix(textureColor, vec4(0.0), min(abs(errX), 1.0));
    }
    if (modeY == 8) {
        textureColor = mix(textureColor, vec4(0.0), min(abs(errY), 1.0));
    }

    // --- ClampToBorderNearest discard ---
    if (modeX == 7) {
        float snappedX = floor(inCoord.x + 0.001) + 0.5;
        if (snappedX < subset.x || snappedX > subset.z) {
            textureColor = vec4(0.0);
        }
    }
    if (modeY == 7) {
        float snappedY = floor(inCoord.y + 0.001) + 0.5;
        if (snappedY < subset.y || snappedY > subset.w) {
            textureColor = vec4(0.0);
        }
    }

    vec4 result = textureColor;

    // --- Alpha-only / premultiply ---
    if (alphaOnly == 1) {
        // Alpha-only textures (R8 format) store alpha in the red channel.
        result = result.rrrr;
        return result.a * inputColor;
    } else {
        return result * inputColor.a;
    }
}

// sampler2DRect overload for macOS Rectangle textures.
vec4 TGFX_TiledTextureEffect(vec4 inputColor, vec2 texCoord, sampler2DRect textureSampler,
                              vec4 subset, vec4 clampRect, vec2 dimension, vec4 extraSubset,
                              int modeX, int modeY, int alphaOnly,
                              int hasDimension, int hasSubset, int hasClamp,
                              int hasStrictConstraint) {
    // Derived boolean flags from mode parameters
    bool unormX = (modeX == 3 || modeX == 4 || modeX == 5 || modeX == 8);
    bool unormY = (modeY == 3 || modeY == 4 || modeY == 5 || modeY == 8);
    bool usesSubsetX = (modeX != 0 && modeX != 1 && modeX != 8);
    bool usesSubsetY = (modeY != 0 && modeY != 1 && modeY != 8);
    bool usesClampX = (modeX != 0 && modeX != 7);
    bool usesClampY = (modeY != 0 && modeY != 7);
    bool mipmapX = (modeX == 4 || modeX == 5);
    bool mipmapY = (modeY == 4 || modeY == 5);
    bool linearRepeatX = (modeX == 3 || modeX == 4);
    bool linearRepeatY = (modeY == 3 || modeY == 4);

    // Fast path: both modes are None
    if (modeX == 0 && modeY == 0) {
        vec4 result = texture(textureSampler, texCoord);
        if (alphaOnly == 1) {
            result = result.rrrr;
            return result.a * inputColor;
        } else {
            return result * inputColor.a;
        }
    }

    vec2 inCoord = texCoord;

    if (hasDimension == 1) {
        inCoord /= dimension;
    }

    // Mipmap repeat variables
    vec2 extraRepeatCoord = vec2(0.0);
    float repeatCoordWeightX = 0.0;
    float repeatCoordWeightY = 0.0;

    // --- Subset coordinate computation ---
    highp vec2 subsetCoord;

    // X axis subset coord
    if (modeX == 0 || modeX == 7 || modeX == 8 || modeX == 1) {
        subsetCoord.x = inCoord.x;
    } else if (modeX == 2 || modeX == 3) {
        subsetCoord.x = mod(inCoord.x - subset.x, subset.z - subset.x) + subset.x;
    } else if (modeX == 4 || modeX == 5) {
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
    } else if (modeX == 6) {
        float w = subset.z - subset.x;
        float w2 = 2.0 * w;
        float m = mod(inCoord.x - subset.x, w2);
        subsetCoord.x = mix(m, w2 - m, step(w, m)) + subset.x;
    }

    // Y axis subset coord
    if (modeY == 0 || modeY == 7 || modeY == 8 || modeY == 1) {
        subsetCoord.y = inCoord.y;
    } else if (modeY == 2 || modeY == 3) {
        subsetCoord.y = mod(inCoord.y - subset.y, subset.w - subset.y) + subset.y;
    } else if (modeY == 4 || modeY == 5) {
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
    } else if (modeY == 6) {
        float w = subset.w - subset.y;
        float w2 = 2.0 * w;
        float m = mod(inCoord.y - subset.y, w2);
        subsetCoord.y = mix(m, w2 - m, step(w, m)) + subset.y;
    }

    // --- Clamp coordinate computation ---
    highp vec2 clampedCoord;

    if (usesClampX && usesClampY) {
        clampedCoord = clamp(subsetCoord, clampRect.xy, clampRect.zw);
    } else if (usesClampX) {
        clampedCoord.x = clamp(subsetCoord.x, clampRect.x, clampRect.z);
        clampedCoord.y = subsetCoord.y;
    } else if (usesClampY) {
        clampedCoord.x = subsetCoord.x;
        clampedCoord.y = clamp(subsetCoord.y, clampRect.y, clampRect.w);
    } else {
        clampedCoord = subsetCoord;
    }

    // --- Strict constraint clamping ---
    if (hasStrictConstraint == 1) {
        if (hasDimension == 1) {
            highp vec4 normExtraSubset = extraSubset;
            normExtraSubset.xy /= dimension;
            normExtraSubset.zw /= dimension;
            clampedCoord = clamp(clampedCoord, normExtraSubset.xy, normExtraSubset.zw);
        } else {
            clampedCoord = clamp(clampedCoord, extraSubset.xy, extraSubset.zw);
        }
    }

    // --- Mipmap repeat extra coord clamping ---
    if (mipmapX && mipmapY) {
        extraRepeatCoord = clamp(extraRepeatCoord, clampRect.xy, clampRect.zw);
    } else if (mipmapX) {
        extraRepeatCoord.x = clamp(extraRepeatCoord.x, clampRect.x, clampRect.z);
    } else if (mipmapY) {
        extraRepeatCoord.y = clamp(extraRepeatCoord.y, clampRect.y, clampRect.w);
    }

    // --- Texture read with mipmap blending ---
    vec4 textureColor;

    if (mipmapX && mipmapY) {
        vec2 sampCoord1 = clampedCoord;
        vec2 sampCoord2 = vec2(extraRepeatCoord.x, clampedCoord.y);
        vec2 sampCoord3 = vec2(clampedCoord.x, extraRepeatCoord.y);
        vec2 sampCoord4 = vec2(extraRepeatCoord.x, extraRepeatCoord.y);
        if (hasDimension == 1) {
            sampCoord1 *= dimension;
            sampCoord2 *= dimension;
            sampCoord3 *= dimension;
            sampCoord4 *= dimension;
        }
        vec4 textureColor1 = texture(textureSampler, sampCoord1);
        vec4 textureColor2 = texture(textureSampler, sampCoord2);
        vec4 textureColor3 = texture(textureSampler, sampCoord3);
        vec4 textureColor4 = texture(textureSampler, sampCoord4);
        textureColor = mix(mix(textureColor1, textureColor2, repeatCoordWeightX),
                           mix(textureColor3, textureColor4, repeatCoordWeightX), repeatCoordWeightY);
    } else if (mipmapX) {
        vec2 sampCoord1 = clampedCoord;
        vec2 sampCoord2 = vec2(extraRepeatCoord.x, clampedCoord.y);
        if (hasDimension == 1) {
            sampCoord1 *= dimension;
            sampCoord2 *= dimension;
        }
        vec4 textureColor1 = texture(textureSampler, sampCoord1);
        vec4 textureColor2 = texture(textureSampler, sampCoord2);
        textureColor = mix(textureColor1, textureColor2, repeatCoordWeightX);
    } else if (mipmapY) {
        vec2 sampCoord1 = clampedCoord;
        vec2 sampCoord2 = vec2(clampedCoord.x, extraRepeatCoord.y);
        if (hasDimension == 1) {
            sampCoord1 *= dimension;
            sampCoord2 *= dimension;
        }
        vec4 textureColor1 = texture(textureSampler, sampCoord1);
        vec4 textureColor2 = texture(textureSampler, sampCoord2);
        textureColor = mix(textureColor1, textureColor2, repeatCoordWeightY);
    } else {
        // Standard read
        vec2 sampCoord = clampedCoord;
        if (hasDimension == 1) {
            sampCoord *= dimension;
        }
        textureColor = texture(textureSampler, sampCoord);
    }

    // --- Linear repeat error-based blending ---
    float errX = 0.0;
    float errY = 0.0;
    float repeatCoordX = 0.0;
    float repeatCoordY = 0.0;

    if (linearRepeatX || modeX == 8) {
        errX = subsetCoord.x - clampedCoord.x;
        if (linearRepeatX) {
            repeatCoordX = errX > 0.0 ? clampRect.x : clampRect.z;
        }
    }

    if (linearRepeatY || modeY == 8) {
        errY = subsetCoord.y - clampedCoord.y;
        if (linearRepeatY) {
            repeatCoordY = errY > 0.0 ? clampRect.y : clampRect.w;
        }
    }

    // Handle RepeatLinear blending for both axes
    if (linearRepeatX && linearRepeatY) {
        vec2 rCoordX = vec2(repeatCoordX, clampedCoord.y);
        vec2 rCoordY = vec2(clampedCoord.x, repeatCoordY);
        vec2 rCoordXY = vec2(repeatCoordX, repeatCoordY);
        if (hasDimension == 1) {
            rCoordX *= dimension;
            rCoordY *= dimension;
            rCoordXY *= dimension;
        }
        vec4 repeatReadX = texture(textureSampler, rCoordX);
        vec4 repeatReadY = texture(textureSampler, rCoordY);
        vec4 repeatReadXY = texture(textureSampler, rCoordXY);
        if (errX != 0.0 && errY != 0.0) {
            errX = abs(errX);
            textureColor = mix(mix(textureColor, repeatReadX, errX),
                               mix(repeatReadY, repeatReadXY, errX), abs(errY));
        } else if (errX != 0.0) {
            vec2 rxCoord = vec2(repeatCoordX, clampedCoord.y);
            if (hasDimension == 1) {
                rxCoord *= dimension;
            }
            vec4 rX = texture(textureSampler, rxCoord);
            textureColor = mix(textureColor, rX, errX);
        } else if (errY != 0.0) {
            vec2 ryCoord = vec2(clampedCoord.x, repeatCoordY);
            if (hasDimension == 1) {
                ryCoord *= dimension;
            }
            vec4 rY = texture(textureSampler, ryCoord);
            textureColor = mix(textureColor, rY, errY);
        }
    } else if (linearRepeatX) {
        if (errX != 0.0) {
            vec2 rxCoord = vec2(repeatCoordX, clampedCoord.y);
            if (hasDimension == 1) {
                rxCoord *= dimension;
            }
            vec4 repeatReadX = texture(textureSampler, rxCoord);
            textureColor = mix(textureColor, repeatReadX, errX);
        }
    } else if (linearRepeatY) {
        if (errY != 0.0) {
            vec2 ryCoord = vec2(clampedCoord.x, repeatCoordY);
            if (hasDimension == 1) {
                ryCoord *= dimension;
            }
            vec4 repeatReadY = texture(textureSampler, ryCoord);
            textureColor = mix(textureColor, repeatReadY, errY);
        }
    }

    // --- ClampToBorderLinear fade ---
    if (modeX == 8) {
        textureColor = mix(textureColor, vec4(0.0), min(abs(errX), 1.0));
    }
    if (modeY == 8) {
        textureColor = mix(textureColor, vec4(0.0), min(abs(errY), 1.0));
    }

    // --- ClampToBorderNearest discard ---
    if (modeX == 7) {
        float snappedX = floor(inCoord.x + 0.001) + 0.5;
        if (snappedX < subset.x || snappedX > subset.z) {
            textureColor = vec4(0.0);
        }
    }
    if (modeY == 7) {
        float snappedY = floor(inCoord.y + 0.001) + 0.5;
        if (snappedY < subset.y || snappedY > subset.w) {
            textureColor = vec4(0.0);
        }
    }

    vec4 result = textureColor;

    // --- Alpha-only / premultiply ---
    if (alphaOnly == 1) {
        // Alpha-only textures (R8 format) store alpha in the red channel.
        result = result.rrrr;
        return result.a * inputColor;
    } else {
        return result * inputColor.a;
    }
}
