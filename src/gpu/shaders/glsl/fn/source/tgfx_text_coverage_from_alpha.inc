// The coverage a text atlas texel carries: its alpha, broadcast to all four channels. The
// unpremultiplied color is a separate operator because the alpha-only form never reads it.
//@function tgfxTextCoverageFromAlpha
//@op TextCoverageFromAlpha
//@frag
vec4 tgfxTextCoverageFromAlpha(vec4 texel /*@in*/) {
  return vec4(texel.a);
}
