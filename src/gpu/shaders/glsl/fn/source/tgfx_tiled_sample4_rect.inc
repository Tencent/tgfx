// The four-tap tiled read for rectangle samplers.
//@function tgfxTiledSample4Rect
vec4 tgfxTiledSample4Rect(sampler2DRect tex, highp vec2 coord00, highp vec2 coord10,
                          highp vec2 coord01, highp vec2 coord11, highp float weightX,
                          highp float weightY) {
  return mix(mix(texture(tex, coord00), texture(tex, coord10), weightX),
             mix(texture(tex, coord01), texture(tex, coord11), weightX), weightY);
}
