// The four-tap tiled read: both axes carry a mipmap seam, the outer mix blends the axis pairs.
//@function tgfxTiledSample4
vec4 tgfxTiledSample4(sampler2D tex, highp vec2 coord00, highp vec2 coord10, highp vec2 coord01,
                      highp vec2 coord11, highp float weightX, highp float weightY) {
  return mix(mix(texture(tex, coord00), texture(tex, coord10), weightX),
             mix(texture(tex, coord01), texture(tex, coord11), weightX), weightY);
}
