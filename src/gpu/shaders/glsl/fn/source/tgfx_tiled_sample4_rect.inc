// The four-tap tiled read for rectangle samplers.
//@function tgfxTiledSample4Rect
//@op TiledSample4Rect
//@frag
vec4 tgfxTiledSample4Rect(sampler2DRect tex, highp vec2 coord00 /*@in*/, highp vec2 coord10 /*@in*/, highp vec2 coord01 /*@in*/, highp vec2 coord11 /*@in*/, highp float weightX /*@in*/, highp float weightY /*@in*/) {
  return mix(mix(texture(tex, coord00), texture(tex, coord10), weightX),
             mix(texture(tex, coord01), texture(tex, coord11), weightX), weightY);
}
