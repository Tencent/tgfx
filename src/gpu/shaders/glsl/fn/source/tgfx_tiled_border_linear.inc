// The linear border fade: the seam error weights the transparent border color in.
//@function tgfxTiledBorderLinear
//@op TiledBorderLinear
//@frag
vec4 tgfxTiledBorderLinear(highp vec4 color /*@in*/, highp float errX /*@in*/, highp float errY /*@in*/, int onX /*@in*/,
                           int onY /*@in*/) {
  if (onX != 0) {
    color = mix(color, vec4(0.0), min(abs(errX), 1.0));
  }
  if (onY != 0) {
    color = mix(color, vec4(0.0), min(abs(errY), 1.0));
  }
  return color;
}
