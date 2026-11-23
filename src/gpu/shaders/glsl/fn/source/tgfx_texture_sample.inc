// The texture read every consumer spells. Call sites keep their swizzle (a channel access, not
// math) and their multi-plane orchestration on their own side. The subset clamp rides the call so
// a clamping reader stays one pure function; a call site with two clamps composes its own.
//@function tgfxTextureSample
vec4 tgfxTextureSample(sampler2D tex, highp vec2 coord, highp vec4 subset, int clampOn,
                       int readSwizzle) {
  highp vec2 finalCoord = clampOn != 0 ? clamp(coord, subset.xy, subset.zw) : coord;
  vec4 color = texture(tex, finalCoord);
  if (readSwizzle == 1) {
    return color.rrrr;
  }
  if (readSwizzle == 2) {
    return color.rrra;
  }
  if (readSwizzle == 3) {
    return color.rgrg;
  }
  if (readSwizzle == 4) {
    return color.aaaa;
  }
  if (readSwizzle == 5) {
    return color.bgra;
  }
  return color;
}
