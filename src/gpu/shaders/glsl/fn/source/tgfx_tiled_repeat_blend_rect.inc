// The repeat seam blend for rectangle samplers.
//@function tgfxTiledRepeatBlendRect
//@op TiledRepeatBlendRect
//@frag
vec4 tgfxTiledRepeatBlendRect(sampler2DRect tex, highp vec4 color /*@in*/, highp vec2 clampedCoord /*@in*/,
                              highp vec2 repeatCoord /*@in*/, highp float errX /*@in*/, highp float errY /*@in*/,
                              int repeatX /*@in*/, int repeatY /*@in*/) {
  vec4 result = color;
  if (repeatX != 0 && repeatY != 0) {
    if (errX != 0.0 && errY != 0.0) {
      highp float ax = abs(errX);
      vec4 readX = texture(tex, vec2(repeatCoord.x, clampedCoord.y));
      vec4 readY = texture(tex, vec2(clampedCoord.x, repeatCoord.y));
      vec4 readXY = texture(tex, vec2(repeatCoord.x, repeatCoord.y));
      result = mix(mix(result, readX, ax), mix(readY, readXY, ax), abs(errY));
    } else if (errX != 0.0) {
      result = mix(result, texture(tex, vec2(repeatCoord.x, clampedCoord.y)), errX);
    } else if (errY != 0.0) {
      result = mix(result, texture(tex, vec2(clampedCoord.x, repeatCoord.y)), errY);
    }
    return result;
  }
  if (repeatX != 0 && errX != 0.0) {
    result = mix(result, texture(tex, vec2(repeatCoord.x, clampedCoord.y)), errX);
  }
  if (repeatY != 0 && errY != 0.0) {
    result = mix(result, texture(tex, vec2(clampedCoord.x, repeatCoord.y)), errY);
  }
  return result;
}
