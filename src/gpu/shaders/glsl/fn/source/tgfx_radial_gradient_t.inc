// The radial-gradient position: the distance from the center.
//@function tgfxRadialGradientT
//@op RadialGradient
//@frag
float tgfxRadialGradientT(highp vec2 coord /*@in*/) {
  return length(coord);
}
