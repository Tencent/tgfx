//@function gradientColorizeDual
vec4 gradientColorizeDual(vec4 scale01, vec4 bias01, vec4 scale23, vec4 bias23, float threshold,
                          float gradT) {
  vec4 gradScale = scale01;
  vec4 gradBias = bias01;
  if (gradT >= threshold) {
    gradScale = scale23;
    gradBias = bias23;
  }
  return vec4(gradT * gradScale + gradBias);
}
