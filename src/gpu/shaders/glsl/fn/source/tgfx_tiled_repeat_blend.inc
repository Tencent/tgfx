// The repeat seam blend: the extra taps land on the mirrored edge, so a zero error keeps the base
// tap. The per-axis flags are the call site's literals.
//@function tgfxTiledRepeatBlend
//@op TiledRepeatBlend
//@frag
vec4 tgfxTiledRepeatBlend(sampler2D tex, highp vec4 color /*@in*/, highp vec2 clampedCoord /*@in*/,
                          highp vec2 repeatCoord /*@in*/, highp float errX /*@in*/, highp float errY /*@in*/, int repeatX /*@in*/,
                          int repeatY /*@in*/) {
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
