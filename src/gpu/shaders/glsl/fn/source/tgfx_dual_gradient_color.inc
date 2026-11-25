// The dual-interval gradient color: the threshold picks the scale/bias pair the position rides.
//@function tgfxDualGradientColor
//@op DualGradientColor
//@frag
vec4 tgfxDualGradientColor(float t /*@in*/, vec4 scale01, vec4 bias01, vec4 scale23, vec4 bias23,
                           float threshold) {
  vec4 scale;
  vec4 bias;
  if (t < threshold) {
    scale = scale01;
    bias = bias01;
  } else {
    scale = scale23;
    bias = bias23;
  }
  return t * scale + bias;
}
