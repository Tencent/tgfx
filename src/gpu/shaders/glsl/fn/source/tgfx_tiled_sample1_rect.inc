// The single-tap tiled read for rectangle samplers.
//@function tgfxTiledSample1Rect
vec4 tgfxTiledSample1Rect(sampler2DRect tex, highp vec2 coord) {
  return texture(tex, coord);
}
