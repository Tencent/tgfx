// The texture read every consumer spells. Call sites keep their swizzle (a channel access, not
// math) and their multi-plane orchestration on their own side.
//@function tgfxTextureSample
vec4 tgfxTextureSample(sampler2D tex, highp vec2 coord) {
  return texture(tex, coord);
}
