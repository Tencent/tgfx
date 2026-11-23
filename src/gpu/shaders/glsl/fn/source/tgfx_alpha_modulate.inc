// The alpha-only modulation: the texel's alpha scales every channel of the input. One call so
// the product is a value a bytecode route can name.
//@function tgfxAlphaModulate
vec4 tgfxAlphaModulate(vec4 texel, vec4 inputColor) {
  return vec4(texel.a) * inputColor;
}
