// The two-tap tiled read: the mipmap seam blends the base tap with its mirrored neighbour.
//@function tgfxTiledSample2
vec4 tgfxTiledSample2(sampler2D tex, highp vec2 coord0, highp vec2 coord1, highp float weight) {
  return mix(texture(tex, coord0), texture(tex, coord1), weight);
}
