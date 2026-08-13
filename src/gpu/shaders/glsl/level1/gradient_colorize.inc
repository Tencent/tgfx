// Shared gradient colorizer: maps t in [0,1] to a color using piecewise linear segments.
// intervalCount is runtime data so all interval counts share this path. Single source for
// gradient_fill.frag and the chain kernel's OP_GRADIENT slot. All interval uniforms are passed
// explicitly so each caller can feed its own uniform names.
//
// Packing (see UnrolledBinaryGradientColorizer::Make): thresholds1_7 holds the interval end
// positions of intervals 0..3 in ascending order, thresholds9_13 those of intervals 4..6; slots
// beyond intervalCount are zero-padded with transparent scales/biases. The selection is a linear
// scan: crossing a real threshold advances to the next interval, and the > 0 guard stops the
// scan at the last real interval, matching the padded-transparent leaves of the legacy binary
// tree for out-of-range t. The selected (scale, bias) pair is identical to the tree's in every
// case, so results are byte-identical.
#ifndef TGFX_GRADIENT_COLORIZE_INC
#define TGFX_GRADIENT_COLORIZE_INC

vec4 gradientColorizeUnrolled(float t, int intervalCount, vec4 thresholds1_7, vec4 thresholds9_13,
                              vec4 scale0_1, vec4 bias0_1, vec4 scale2_3, vec4 bias2_3,
                              vec4 scale4_5, vec4 bias4_5, vec4 scale6_7, vec4 bias6_7,
                              vec4 scale8_9, vec4 bias8_9, vec4 scale10_11, vec4 bias10_11,
                              vec4 scale12_13, vec4 bias12_13, vec4 scale14_15, vec4 bias14_15) {
  vec4 scale = scale0_1;
  vec4 bias = bias0_1;
  if (thresholds1_7.x > 0.0 && t >= thresholds1_7.x) {
    scale = scale2_3;
    bias = bias2_3;
  }
  if (thresholds1_7.y > 0.0 && t >= thresholds1_7.y) {
    scale = scale4_5;
    bias = bias4_5;
  }
  if (thresholds1_7.z > 0.0 && t >= thresholds1_7.z) {
    scale = scale6_7;
    bias = bias6_7;
  }
  if (thresholds1_7.w > 0.0 && t >= thresholds1_7.w) {
    scale = scale8_9;
    bias = bias8_9;
  }
  if (thresholds9_13.x > 0.0 && t >= thresholds9_13.x) {
    scale = scale10_11;
    bias = bias10_11;
  }
  if (thresholds9_13.y > 0.0 && t >= thresholds9_13.y) {
    scale = scale12_13;
    bias = bias12_13;
  }
  if (thresholds9_13.z > 0.0 && t >= thresholds9_13.z) {
    scale = scale14_15;
    bias = bias14_15;
  }
  return vec4(t * scale + bias);
}

#endif
