// GradientFillShader fragment shader
// Processor layout: DefaultGeometryProcessor() + ClampedGradientEffect() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected by build tool as #define):
//   LAYOUT_TYPE: 0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend), 2=PorterDuff FBF
// INTERVAL_COUNT is a runtime uniform (1~8), no longer a compile-time dimension.
#version 450

#ifndef LAYOUT_TYPE
#define LAYOUT_TYPE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 leftBorderColor;
  vec4 rightBorderColor;
#if LAYOUT_TYPE == 2
  float Bias;
  float Scale;
#endif
  int IntervalCount;
  vec4 thresholds1_7;
  vec4 thresholds9_13;
  vec4 scale0_1;
  vec4 scale2_3;
  vec4 scale4_5;
  vec4 scale6_7;
  vec4 scale8_9;
  vec4 scale10_11;
  vec4 scale12_13;
  vec4 scale14_15;
  vec4 bias0_1;
  vec4 bias2_3;
  vec4 bias4_5;
  vec4 bias6_7;
  vec4 bias8_9;
  vec4 bias10_11;
  vec4 bias12_13;
  vec4 bias14_15;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

#if HAS_COVERAGE == 2
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 1
#else
  #define XP_DST_TEX_BINDING 0
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

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
// IntervalCount is a runtime uniform so all interval counts share the same shader variant.
// The binary search tree structure mirrors the compile-time version exactly.
vec4 colorize(float t) {
  vec4 scale, bias;

  if (IntervalCount >= 4) {
    // thresholds1_7.w splits intervals (0..7) from (8..15)
    if (t < thresholds1_7.w) {
      // thresholds1_7.y splits intervals (0..3) from (4..7)
      if (t < thresholds1_7.y) {
        // thresholds1_7.x splits intervals (0,1) from (2,3)
        if (t < thresholds1_7.x) {
          scale = scale0_1;
          bias = bias0_1;
        } else {
          scale = scale2_3;
          bias = bias2_3;
        }
      } else {
        // thresholds1_7.z splits intervals (4,5) from (6,7)
        if (t < thresholds1_7.z) {
          scale = scale4_5;
          bias = bias4_5;
        } else {
          scale = scale6_7;
          bias = bias6_7;
        }
      }
    } else {
      if (IntervalCount >= 6) {
        // thresholds9_13.y splits intervals (8..11) from (12..15)
        if (t < thresholds9_13.y) {
          // thresholds9_13.x splits intervals (8,9) from (10,11)
          if (t < thresholds9_13.x) {
            scale = scale8_9;
            bias = bias8_9;
          } else {
            scale = scale10_11;
            bias = bias10_11;
          }
        } else {
          if (IntervalCount >= 7) {
            // thresholds9_13.z splits intervals (12,13) from (14,15)
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
        // IntervalCount is 4 or 5
        // thresholds9_13.x splits intervals (8,9) from (10,11)
        if (t < thresholds9_13.x) {
          scale = scale8_9;
          bias = bias8_9;
        } else {
          scale = scale10_11;
          bias = bias10_11;
        }
      }
    }
  } else if (IntervalCount >= 2) {
    // thresholds1_7.y splits intervals (0..3) from (4..7)
    if (t < thresholds1_7.y) {
      // thresholds1_7.x splits intervals (0,1) from (2,3)
      if (t < thresholds1_7.x) {
        scale = scale0_1;
        bias = bias0_1;
      } else {
        scale = scale2_3;
        bias = bias2_3;
      }
    } else {
      if (IntervalCount >= 3) {
        // thresholds1_7.z splits intervals (4,5) from (6,7)
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
    // IntervalCount == 1: single interval covers full range
    scale = scale0_1;
    bias = bias0_1;
  }

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

#define TGFX_COVERAGE_SRC_COLOR gradColor
#include "coverage_output.inc"
#include "xp_output.inc"
}
