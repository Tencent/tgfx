// Pure texture modulation fragment: the entry fetches the leaf into `tex`, this fragment owns
// only the modulation rules. `flags` is the selector's low five bits, a literal at the call site:
// bit 0 paint-alpha modulation, bit 1 alpha-only splat, bit 2 coverage-root modulation, bit 3
// geometry-RGB modulation of an alpha-only leaf, bit 4 full-geometry modulation.
vec4 tgfxOpTexture(vec4 tex, vec4 geomColor, vec4 unitCoverage, int flags) {
  if ((flags & 2) != 0) {
    tex = aotTextureAlphaOnly(tex);
    if ((flags & 8) != 0) {
      tex = aotTextureModulate(tex, vec4(geomColor.rgb, 1.0), true);
    }
    if ((flags & 16) != 0) {
      tex = aotTextureModulate(tex, geomColor, true);
    }
  }
  if ((flags & 4) != 0) {
    tex = aotTextureModulate(tex, unitCoverage, false);
  }
  if ((flags & 1) != 0) {
    tex = aotTextureModulate(tex, geomColor, false);
  }
  return tex;
}
