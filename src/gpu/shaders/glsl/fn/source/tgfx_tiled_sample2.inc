// The two-tap tiled read: the mipmap seam blends the base tap with its mirrored neighbour.
//@function tgfxTiledSample2
//@op TiledSample2
//@frag
vec4 tgfxTiledSample2(sampler2D tex, highp vec2 coord0 /*@in*/, highp vec2 coord1 /*@in*/, highp float weight /*@in*/) {
  return mix(texture(tex, coord0), texture(tex, coord1), weight);
}
