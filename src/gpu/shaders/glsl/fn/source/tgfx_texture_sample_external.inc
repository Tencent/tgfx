// The texture read for external (camera/video) samplers. The text is only inserted into shaders
// that actually sample an external texture, so the platform-specific sampler type never reaches a
// profile that does not declare it.
//@function tgfxTextureSampleExternal
vec4 tgfxTextureSampleExternal(samplerExternalOES tex, highp vec2 coord) {
  return texture(tex, coord);
}
