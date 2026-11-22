// The diamond-gradient position: the Chebyshev distance from the center.
//@function tgfxDiamondGradientT
float tgfxDiamondGradientT(highp vec2 coord) {
  return max(abs(coord.x), abs(coord.y));
}
