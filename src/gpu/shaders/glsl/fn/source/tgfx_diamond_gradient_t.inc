// The diamond-gradient position: the Chebyshev distance from the center.
//@function tgfxDiamondGradientT
//@op DiamondGradient
//@frag
float tgfxDiamondGradientT(highp vec2 coord /*@in*/) {
  return max(abs(coord.x), abs(coord.y));
}
