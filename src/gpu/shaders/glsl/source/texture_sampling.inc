#ifndef TGFX_TEXTURE_SAMPLING_INC
#define TGFX_TEXTURE_SAMPLING_INC

#include "source/texture_read_swizzle.inc"

highp vec2 aotTextureClamp(highp vec2 coord, highp vec4 subset, bool enabled) {
  if (enabled) {
    return clamp(coord, subset.xy, subset.zw);
  }
  return coord;
}

vec4 aotTextureRead(sampler2D texSampler, highp vec2 coord, int readSwizzle) {
  return aotReadSwizzle(texture(texSampler, coord), readSwizzle);
}

vec4 aotTextureAlphaOnly(vec4 color) {
  return vec4(color.r);
}

vec4 aotTextureDecodeRGBAAA(vec4 color, vec4 alpha) {
  color = clamp(color, 0.0, 1.0);
  alpha = clamp(alpha, 0.0, 1.0);
  return vec4(color.rgb * alpha.r, alpha.r);
}

vec4 aotTextureModulate(vec4 color, vec4 inputColor, bool modulateRGBA) {
  if (modulateRGBA) {
    return color * inputColor;
  }
  return color * inputColor.a;
}

// Coordinates are already perspective-divided by the caller. Subset gating and channel decode
// order are explicit; alpha-only is applied before RGBAAA, matching TextureEffect.
vec4 aotTextureSample(sampler2D texSampler, highp vec2 coord, vec4 subset, int subsetClampOn,
                      vec2 alphaStart, int alphaOnly, int hasRgbaaa, int readSwizzle) {
  highp vec2 finalCoord = aotTextureClamp(coord, subset, subsetClampOn != 0);
  vec4 color = aotTextureRead(texSampler, finalCoord, readSwizzle);
  if (alphaOnly != 0) {
    color = aotTextureAlphaOnly(color);
  }
  if (hasRgbaaa != 0) {
    vec4 alpha = aotTextureRead(texSampler, finalCoord + alphaStart, readSwizzle);
    color = aotTextureDecodeRGBAAA(color, alpha);
  }
  return color;
}

vec4 aotTextureSource(sampler2D texSampler, highp vec2 coord, vec4 inputColor, vec4 subset,
                      int subsetClampOn, vec2 alphaStart, int alphaOnly, int hasRgbaaa,
                      int readSwizzle) {
  vec4 color = aotTextureSample(texSampler, coord, subset, subsetClampOn, alphaStart, alphaOnly,
                               hasRgbaaa, readSwizzle);
  return aotTextureModulate(color, inputColor, alphaOnly != 0);
}

#endif
