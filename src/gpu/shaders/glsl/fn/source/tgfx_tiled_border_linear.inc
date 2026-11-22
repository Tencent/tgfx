// The linear border fade: the seam error weights the transparent border color in.
//@function tgfxTiledBorderLinear
vec4 tgfxTiledBorderLinear(highp vec4 color, highp float errX, highp float errY, int onX,
                           int onY) {
  if (onX != 0) {
    color = mix(color, vec4(0.0), min(abs(errX), 1.0));
  }
  if (onY != 0) {
    color = mix(color, vec4(0.0), min(abs(errY), 1.0));
  }
  return color;
}
