//@function gradientColorizeUnrolled
vec4 gradientColorizeUnrolled(float t, vec4 thresholds1_7, vec4 thresholds9_13, vec4 scales[8], vec4 biases[8]) {
  vec4 scale = scales[0];
  vec4 bias = biases[0];
  if (thresholds1_7.x > 0.0 && t >= thresholds1_7.x) {
    scale = scales[1];
    bias = biases[1];
  }
  if (thresholds1_7.y > 0.0 && t >= thresholds1_7.y) {
    scale = scales[2];
    bias = biases[2];
  }
  if (thresholds1_7.z > 0.0 && t >= thresholds1_7.z) {
    scale = scales[3];
    bias = biases[3];
  }
  if (thresholds1_7.w > 0.0 && t >= thresholds1_7.w) {
    scale = scales[4];
    bias = biases[4];
  }
  if (thresholds9_13.x > 0.0 && t >= thresholds9_13.x) {
    scale = scales[5];
    bias = biases[5];
  }
  if (thresholds9_13.y > 0.0 && t >= thresholds9_13.y) {
    scale = scales[6];
    bias = biases[6];
  }
  if (thresholds9_13.z > 0.0 && t >= thresholds9_13.z) {
    scale = scales[7];
    bias = biases[7];
  }
  return vec4(t * scale + bias);
}
