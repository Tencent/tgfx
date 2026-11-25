// The single-tap tiled read for rectangle samplers.
//@function tgfxTiledSample1Rect
//@op TiledSample1Rect
//@frag
vec4 tgfxTiledSample1Rect(sampler2DRect tex, highp vec2 coord /*@in*/) {
  return texture(tex, coord);
}
