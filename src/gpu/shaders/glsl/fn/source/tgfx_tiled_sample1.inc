// The single-tap tiled read.
//@function tgfxTiledSample1
vec4 tgfxTiledSample1(sampler2D tex, highp vec2 coord) {
  return texture(tex, coord);
}
