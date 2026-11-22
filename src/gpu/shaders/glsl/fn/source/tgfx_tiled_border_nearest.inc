// The nearest border: a snapped texel outside the subset reads the transparent border color.
//@function tgfxTiledBorderNearest
vec4 tgfxTiledBorderNearest(highp vec4 color, highp vec2 inCoord, highp vec4 subset, int modeX,
                            int modeY) {
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
