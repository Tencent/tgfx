// Pure tex-modulate fragment: a raw leaf sample modulated by a computed input. Alpha-only is
// sample.a * inputColor, RGBA is sample * inputColor.a. The entry supplies the fetched sample.
vec4 tgfxOpTexModulate(vec4 tex, vec4 in0, int alphaOnly) {
  if (alphaOnly != 0) {
    tex = aotTextureAlphaOnly(tex);
  }
  return aotTextureModulate(tex, in0, alphaOnly != 0);
}
