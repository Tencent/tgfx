// The conic-gradient position: the angle mapped through bias and scale.
//@function tgfxConicGradientT
float tgfxConicGradientT(highp vec2 coord, highp float bias, highp float scale) {
  highp float angle = atan(-coord.y, -coord.x);
  return ((angle * 0.15915494309180001 + 0.5) + bias) * scale;
}
