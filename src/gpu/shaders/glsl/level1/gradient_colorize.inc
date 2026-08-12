// Shared unrolled-binary gradient colorizer: maps t in [0,1] to a color using piecewise linear
// segments. intervalCount is runtime data so all interval counts share this path; the binary
// search tree structure mirrors the compile-time colorizer exactly. Single source for
// gradient_fill.frag and the chain kernel's OP_GRADIENT slot. All interval uniforms are passed
// explicitly so each caller can feed its own uniform names.
#ifndef TGFX_GRADIENT_COLORIZE_INC
#define TGFX_GRADIENT_COLORIZE_INC

vec4 gradientColorizeUnrolled(float t, int intervalCount, vec4 thresholds1_7, vec4 thresholds9_13,
                              vec4 scale0_1, vec4 bias0_1, vec4 scale2_3, vec4 bias2_3,
                              vec4 scale4_5, vec4 bias4_5, vec4 scale6_7, vec4 bias6_7,
                              vec4 scale8_9, vec4 bias8_9, vec4 scale10_11, vec4 bias10_11,
                              vec4 scale12_13, vec4 bias12_13, vec4 scale14_15, vec4 bias14_15) {
  vec4 scale;
  vec4 bias;
  if (intervalCount >= 4) {
    if (t < thresholds1_7.w) {
      if (t < thresholds1_7.y) {
        if (t < thresholds1_7.x) {
          scale = scale0_1;
          bias = bias0_1;
        } else {
          scale = scale2_3;
          bias = bias2_3;
        }
      } else {
        if (t < thresholds1_7.z) {
          scale = scale4_5;
          bias = bias4_5;
        } else {
          scale = scale6_7;
          bias = bias6_7;
        }
      }
    } else {
      if (intervalCount >= 6) {
        if (t < thresholds9_13.y) {
          if (t < thresholds9_13.x) {
            scale = scale8_9;
            bias = bias8_9;
          } else {
            scale = scale10_11;
            bias = bias10_11;
          }
        } else {
          if (intervalCount >= 7) {
            if (t < thresholds9_13.z) {
              scale = scale12_13;
              bias = bias12_13;
            } else {
              scale = scale14_15;
              bias = bias14_15;
            }
          } else {
            scale = scale12_13;
            bias = bias12_13;
          }
        }
      } else {
        if (t < thresholds9_13.x) {
          scale = scale8_9;
          bias = bias8_9;
        } else {
          scale = scale10_11;
          bias = bias10_11;
        }
      }
    }
  } else if (intervalCount >= 2) {
    if (t < thresholds1_7.y) {
      if (t < thresholds1_7.x) {
        scale = scale0_1;
        bias = bias0_1;
      } else {
        scale = scale2_3;
        bias = bias2_3;
      }
    } else {
      if (intervalCount >= 3) {
        if (t < thresholds1_7.z) {
          scale = scale4_5;
          bias = bias4_5;
        } else {
          scale = scale6_7;
          bias = bias6_7;
        }
      } else {
        scale = scale4_5;
        bias = bias4_5;
      }
    }
  } else {
    scale = scale0_1;
    bias = bias0_1;
  }
  return vec4(t * scale + bias);
}

#endif
