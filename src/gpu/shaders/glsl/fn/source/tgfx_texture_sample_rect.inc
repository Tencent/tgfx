// The texture read for rectangle samplers: the same read as tgfxTextureSample, whose parameter
// type cannot accept a sampler2DRect (GLSL matches sampler types exactly, and one name cannot
// serve several sampler types).
//@function tgfxTextureSampleRect
vec4 tgfxTextureSampleRect(sampler2DRect tex, highp vec2 coord) {
  return texture(tex, coord);
}
