// The single-tap tiled read.
//@function tgfxTiledSample1
//@op TiledSample1
//@frag
vec4 tgfxTiledSample1(sampler2D tex, highp vec2 coord /*@in*/) {
  return texture(tex, coord);
}
