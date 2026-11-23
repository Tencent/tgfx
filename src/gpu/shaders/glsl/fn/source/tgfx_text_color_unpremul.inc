// The unpremultiplied color a text atlas texel carries: rgb over alpha, clamped, alpha forced to
// one. This is the exact arithmetic the emit site used to spell, kept as one named value.
//@function tgfxTextColorUnpremul
vec4 tgfxTextColorUnpremul(vec4 texel) {
  return clamp(vec4(texel.rgb / texel.a, 1.0), 0.0, 1.0);
}
