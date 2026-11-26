// The subset clamp that precedes a texture read.
//@function tgfxClampSubset
//@op ClampSubset
//@frag
vec2 tgfxClampSubset(highp vec2 coord /*@in*/, highp vec4 subset) {
  return clamp(coord, subset.xy, subset.zw);
}
