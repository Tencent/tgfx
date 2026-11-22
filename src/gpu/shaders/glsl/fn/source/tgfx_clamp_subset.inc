// The subset clamp that precedes a texture read.
//@function tgfxClampSubset
vec2 tgfxClampSubset(highp vec2 coord, highp vec4 subset) {
  return clamp(coord, subset.xy, subset.zw);
}
