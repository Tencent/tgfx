// The unrolled binary gradient: one interval search over the packed thresholds, ending in that
// interval's scale/bias pair. Each threshold packs an interval end position; the thresholds beyond
// intervalCount are zero padding, so the reads must be guarded or they would match every t. Past
// the last end the gradient holds the last interval, which is the raster semantics.
//@function tgfxUnrolledBinaryGradient
//@op UnrolledBinaryGradient
//@frag
vec4 tgfxUnrolledBinaryGradient(vec4 color /*@in*/, int intervalCount /*@in*/,
                                vec4 scale0_1 /*@in*/, vec4 scale2_3 /*@in*/,
                                vec4 scale4_5 /*@in*/, vec4 scale6_7 /*@in*/,
                                vec4 scale8_9 /*@in*/, vec4 scale10_11 /*@in*/,
                                vec4 scale12_13 /*@in*/, vec4 scale14_15 /*@in*/,
                                vec4 bias0_1 /*@in*/, vec4 bias2_3 /*@in*/,
                                vec4 bias4_5 /*@in*/, vec4 bias6_7 /*@in*/,
                                vec4 bias8_9 /*@in*/, vec4 bias10_11 /*@in*/,
                                vec4 bias12_13 /*@in*/, vec4 bias14_15 /*@in*/,
                                vec4 thresholds1_7 /*@in*/, vec4 thresholds9_13 /*@in*/) {
  float t = color.x;
  int index = 0;
  if (t >= thresholds1_7.x) {
    index = 1;
  }
  if (intervalCount > 1 && t >= thresholds1_7.y) {
    index = 2;
  }
  if (intervalCount > 2 && t >= thresholds1_7.z) {
    index = 3;
  }
  if (intervalCount > 3 && t >= thresholds1_7.w) {
    index = 4;
  }
  if (intervalCount > 4 && t >= thresholds9_13.x) {
    index = 5;
  }
  if (intervalCount > 5 && t >= thresholds9_13.y) {
    index = 6;
  }
  if (intervalCount > 6 && t >= thresholds9_13.z) {
    index = 7;
  }
  index = min(index, intervalCount - 1);
  vec4 scale = scale0_1;
  vec4 bias = bias0_1;
  if (index == 1) {
    scale = scale2_3;
    bias = bias2_3;
  } else if (index == 2) {
    scale = scale4_5;
    bias = bias4_5;
  } else if (index == 3) {
    scale = scale6_7;
    bias = bias6_7;
  } else if (index == 4) {
    scale = scale8_9;
    bias = bias8_9;
  } else if (index == 5) {
    scale = scale10_11;
    bias = bias10_11;
  } else if (index == 6) {
    scale = scale12_13;
    bias = bias12_13;
  } else if (index == 7) {
    scale = scale14_15;
    bias = bias14_15;
  }
  return vec4(t * scale + bias);
}
