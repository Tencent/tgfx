// The four-tap tiled read: both axes carry a mipmap seam, the outer mix blends the axis pairs.
//@function tgfxTiledSample4
//@op TiledSample4
//@frag
vec4 tgfxTiledSample4(sampler2D tex, highp vec2 coord00 /*@in*/, highp vec2 coord10 /*@in*/, highp vec2 coord01 /*@in*/, highp vec2 coord11 /*@in*/, highp float weightX /*@in*/, highp float weightY /*@in*/) {
  return mix(mix(texture(tex, coord00), texture(tex, coord10), weightX),
             mix(texture(tex, coord01), texture(tex, coord11), weightX), weightY);
}
