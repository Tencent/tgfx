// The color multiplied by another color's alpha: the premultiplied operand the blend formulas
// read, spelled as its own call so every argument a call site passes is a name or a literal.
//@function tgfxMulAlpha
//@op MulAlpha
//@frag
vec4 tgfxMulAlpha(vec4 color /*@in*/, vec4 alphaSource /*@in*/) {
  return color * alphaSource.a;
}
