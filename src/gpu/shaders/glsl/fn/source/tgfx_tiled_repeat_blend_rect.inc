// The repeat seam blend for rectangle samplers.
//@function tgfxTiledRepeatBlendRect
vec4 tgfxTiledRepeatBlendRect(sampler2DRect tex, highp vec4 color, highp vec2 clampedCoord,
                              highp vec2 repeatCoord, highp float errX, highp float errY,
                              int repeatX, int repeatY) {
  if (repeatX != 0 && repeatY != 0) {
    if (errX != 0.0 && errY != 0.0) {
      highp float ax = abs(errX);
      vec4 readX = texture(tex, vec2(repeatCoord.x, clampedCoord.y));
      vec4 readY = texture(tex, vec2(clampedCoord.x, repeatCoord.y));
      vec4 readXY = texture(tex, vec2(repeatCoord.x, repeatCoord.y));
      color = mix(mix(color, readX, ax), mix(readY, readXY, ax), abs(errY));
    } else if (errX != 0.0) {
      color = mix(color, texture(tex, vec2(repeatCoord.x, clampedCoord.y)), errX);
    } else if (errY != 0.0) {
      color = mix(color, texture(tex, vec2(clampedCoord.x, repeatCoord.y)), errY);
    }
    return color;
  }
  if (repeatX != 0 && errX != 0.0) {
    color = mix(color, texture(tex, vec2(repeatCoord.x, clampedCoord.y)), errX);
  }
  if (repeatY != 0 && errY != 0.0) {
    color = mix(color, texture(tex, vec2(clampedCoord.x, repeatCoord.y)), errY);
  }
  return color;
}
