// GradientFillShader fragment shader
// Processor layout: DefaultGeometryProcessor() + ClampedGradientEffect() + EmptyXferProcessor()
// Permutation dimensions (injected by build tool as #define):
//   LAYOUT_TYPE: 0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND
//   INTERVAL_COUNT: 0..7 (maps to actual interval count 1..8)
#version 450

#ifndef LAYOUT_TYPE
#define LAYOUT_TYPE 0
#endif
#ifndef INTERVAL_COUNT
#define INTERVAL_COUNT 0
#endif

// Actual interval count is the dimension value + 1 (dimension range [0,7] -> intervals [1,8])
#define INTERVALS (INTERVAL_COUNT + 1)

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 leftBorderColor;
  vec4 rightBorderColor;
#if LAYOUT_TYPE == 2
  float Bias;
  float Scale;
#endif
  // Colorizer uniforms: thresholds always present
  vec4 thresholds1_7;
  vec4 thresholds9_13;
  // Scale/bias pairs: only those needed for the interval count
  vec4 scale0_1;
#if INTERVALS > 1
  vec4 scale2_3;
#endif
#if INTERVALS > 2
  vec4 scale4_5;
#endif
#if INTERVALS > 3
  vec4 scale6_7;
#endif
#if INTERVALS > 4
  vec4 scale8_9;
#endif
#if INTERVALS > 5
  vec4 scale10_11;
#endif
#if INTERVALS > 6
  vec4 scale12_13;
#endif
#if INTERVALS > 7
  vec4 scale14_15;
#endif
  vec4 bias0_1;
#if INTERVALS > 1
  vec4 bias2_3;
#endif
#if INTERVALS > 2
  vec4 bias4_5;
#endif
#if INTERVALS > 3
  vec4 bias6_7;
#endif
#if INTERVALS > 4
  vec4 bias8_9;
#endif
#if INTERVALS > 5
  vec4 bias10_11;
#endif
#if INTERVALS > 6
  vec4 bias12_13;
#endif
#if INTERVALS > 7
  vec4 bias14_15;
#endif
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(location = 0) out vec4 fragColor;

// Compute the gradient parameter t from transformed coordinates based on layout type.
float computeLayoutT(vec2 coord) {
#if LAYOUT_TYPE == 0
  // Linear: t = x
  return coord.x + 1.0000000000000001e-05;
#elif LAYOUT_TYPE == 1
  // Radial: t = length
  return length(coord);
#elif LAYOUT_TYPE == 2
  // Conic: t = angle-based
  float angle = atan(-coord.y, -coord.x);
  return ((angle * 0.15915494309180001 + 0.5) + Bias) * Scale;
#elif LAYOUT_TYPE == 3
  // Diamond: t = max(|x|, |y|)
  return max(abs(coord.x), abs(coord.y));
#endif
}

// Unrolled binary gradient colorizer: maps t in [0,1] to a color using piecewise linear segments.
vec4 colorize(float t) {
  vec4 scale, bias;

#if INTERVALS >= 4
  // Split at thresholds1_7.w (midpoint between intervals 0-7 and 8-15)
  if (t < thresholds1_7.w) {
#endif

#if INTERVALS >= 2
  // Split at thresholds1_7.y (midpoint between intervals 0-3 and 4-7)
  if (t < thresholds1_7.y) {
#endif
  // Split at thresholds1_7.x (midpoint between intervals 0-1 and 2-3)
  if (t < thresholds1_7.x) {
    scale = scale0_1;
    bias = bias0_1;
#if INTERVALS > 1
  } else {
    scale = scale2_3;
    bias = bias2_3;
#endif
  }
#if INTERVALS > 2
  } else {
  // Split at thresholds1_7.z (midpoint between intervals 4-5 and 6-7)
  if (t < thresholds1_7.z) {
    scale = scale4_5;
    bias = bias4_5;
#if INTERVALS > 3
  } else {
    scale = scale6_7;
    bias = bias6_7;
#endif
  }
#endif

#if INTERVALS >= 2
  }
#endif

#if INTERVALS > 4
  } else {
#endif

#if INTERVALS >= 6
  // Split at thresholds9_13.y (midpoint between intervals 8-11 and 12-15)
  if (t < thresholds9_13.y) {
#endif
#if INTERVALS >= 5
  // Split at thresholds9_13.x (midpoint between intervals 8-9 and 10-11)
  if (t < thresholds9_13.x) {
    scale = scale8_9;
    bias = bias8_9;
#if INTERVALS > 5
  } else {
    scale = scale10_11;
    bias = bias10_11;
#endif
  }
#endif
#if INTERVALS > 6
  } else {
  // Split at thresholds9_13.z (midpoint between intervals 12-13 and 14-15)
  if (t < thresholds9_13.z) {
    scale = scale12_13;
    bias = bias12_13;
#if INTERVALS > 7
  } else {
    scale = scale14_15;
    bias = bias14_15;
#endif
  }
#endif
#if INTERVALS >= 6
  }
#endif

#if INTERVALS >= 4
  }
#endif

  return vec4(t * scale + bias);
}

void main() {
  vec4 outputColor = Color;
  highp vec2 coord = TransformedCoords_0;

  // Layout: compute t
  float t = computeLayoutT(coord);

  // ClampedGradientEffect: border clamping + colorization
  vec4 gradColor;
  if (t <= 0.0) {
    gradColor = leftBorderColor;
  } else if (t >= 1.0) {
    gradColor = rightBorderColor;
  } else {
    gradColor = colorize(t);
  }

  // Premultiply alpha
  gradColor.rgb *= gradColor.a;
  // Multiply by input color alpha (from DefaultGeometryProcessor)
  gradColor *= outputColor.a;

  // EmptyXferProcessor — passthrough
  fragColor = gradColor;
}
