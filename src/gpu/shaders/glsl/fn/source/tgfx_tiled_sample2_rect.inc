// The two-tap tiled read for rectangle samplers.
//@function tgfxTiledSample2Rect
//@op TiledSample2Rect
//@frag
vec4 tgfxTiledSample2Rect(sampler2DRect tex, highp vec2 coord0 /*@in*/, highp vec2 coord1 /*@in*/, highp float weight /*@in*/) {
  return mix(texture(tex, coord0), texture(tex, coord1), weight);
}
