// The linear-gradient position: the ramp adds the epsilon the runtime emission uses to keep the
// first stop off the boundary.
//@function tgfxLinearGradientT
float tgfxLinearGradientT(highp vec2 coord) {
  return coord.x + 1.0000000000000001e-05;
}
