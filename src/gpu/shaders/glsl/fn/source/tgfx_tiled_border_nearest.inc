// The nearest border: a snapped texel outside the subset reads the transparent border color.
//@function tgfxTiledBorderNearest
//@op TiledBorderNearest
//@frag
vec4 tgfxTiledBorderNearest(highp vec4 color /*@in*/, highp vec2 inCoord /*@in*/, highp vec4 subset /*@in*/, int modeX /*@in*/,
                            int modeY /*@in*/) {
  if (modeX != 0) {
    highp float snappedX = floor(inCoord.x + 0.001) + 0.5;
    if (snappedX < subset.x || snappedX > subset.z) {
      color = vec4(0.0);
    }
  }
  if (modeY != 0) {
    highp float snappedY = floor(inCoord.y + 0.001) + 0.5;
    if (snappedY < subset.y || snappedY > subset.w) {
      color = vec4(0.0);
    }
  }
  return color;
}
