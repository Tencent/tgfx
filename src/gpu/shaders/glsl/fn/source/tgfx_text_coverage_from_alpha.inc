// The coverage a text atlas texel carries: its alpha, broadcast to all four channels. The
// unpremultiplied color is a separate operator because the alpha-only form never reads it.
//@function tgfxTextCoverageFromAlpha
vec4 tgfxTextCoverageFromAlpha(vec4 texel) {
  return vec4(texel.a);
}
