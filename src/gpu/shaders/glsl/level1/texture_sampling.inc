#ifndef TGFX_TEXTURE_SAMPLING_INC
#define TGFX_TEXTURE_SAMPLING_INC

#include "texture_read_swizzle.inc"

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

#endif
