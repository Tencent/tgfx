// The two-tap tiled read for rectangle samplers.
//@function tgfxTiledSample2Rect
vec4 tgfxTiledSample2Rect(sampler2DRect tex, highp vec2 coord0, highp vec2 coord1, highp float weight) {
  return mix(texture(tex, coord0), texture(tex, coord1), weight);
}
