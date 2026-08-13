// UnifiedGradientShader fragment shader.
// Processor layout: GP + ClampedGradientEffect() + EmptyXferProcessor/PorterDuffXP
// One kernel serves all four gradient colorizers: the colorizer is selected at runtime through the
// ColorizerKind uniform (0 = single-interval mix, 1 = dual-interval scale/bias, 2 = unrolled
// binary, 3 = LUT texture), mirroring the chain kernel's OP_GRADIENT slot. Only the LUT colorizer
// carries a sampler, so it alone gets a compile-time dimension (HAS_LUT); every other distinction
// is runtime data.
// Permutation dimensions (injected by build tool as #define):
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend), 2=PorterDuff FBF
//   HAS_DEVICE_MASK: device-space alpha mask sampled after colorization
//   HAS_VCOVERAGE: per-vertex AA coverage varying
//   HAS_LUT: the LUT gradient texture sampler is bound
// Runtime uniforms:
//   LayoutType (int): 0=LINEAR, 1=RADIAL, 2=CONIC, 3=DIAMOND
//   ColorizerKind (int): 0..3 as above
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1
#define HAS_RUNTIME_DEVICE_MASK 1
#ifndef HAS_DEVICE_MASK
#define HAS_DEVICE_MASK 0
#endif
#ifndef HAS_VCOVERAGE
#define HAS_VCOVERAGE 0
#endif
#ifndef HAS_LUT
#define HAS_LUT 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 leftBorderColor;
  vec4 rightBorderColor;
  int LayoutType;
  float Bias;
  float Scale;
  int ColorizerKind;
  // SingleIntervalGradientColorizer
  vec4 start;
  vec4 end;
  // DualIntervalGradientColorizer
  vec4 scale01;
  vec4 bias01;
  vec4 scale23;
  vec4 bias23;
  float threshold;
  // UnrolledBinaryGradientColorizer
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
#if HAS_VCOVERAGE
layout(location = 1) in float vCoverage;
#endif

#if HAS_LUT
layout(set = 1, binding = 0) uniform sampler2D GradientTexture;
#endif
// Always bound: an absent device mask is padded with the shared dummy texture and
// HasDeviceMask is 0.
layout(set = 1, binding = HAS_LUT) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING (HAS_LUT + 1)
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

#include "gradient_layout.inc"
#include "gradient_colorize.inc"

void main() {
  vec4 outputColor = Color;
  highp vec2 coord = TransformedCoords_0;

  float t = gradientLayoutT(LayoutType, Bias, Scale, coord);

  vec4 gradColor;
  if (t <= 0.0) {
    gradColor = leftBorderColor;
  } else if (t >= 1.0) {
    gradColor = rightBorderColor;
  } else if (ColorizerKind == 0) {
    gradColor = mix(start, end, t);
  } else if (ColorizerKind == 1) {
    vec4 scale = scale01;
    vec4 bias = bias01;
    if (t >= threshold) {
      scale = scale23;
      bias = bias23;
    }
    gradColor = vec4(t * scale + bias);
  } else if (ColorizerKind == 3) {
#if HAS_LUT
    gradColor = texture(GradientTexture, vec2(t, 0.5));
#else
    gradColor = vec4(0.0);
#endif
  } else {
    gradColor = gradientColorizeUnrolled(t, IntervalCount, thresholds1_7, thresholds9_13, scale0_1,
                                         bias0_1, scale2_3, bias2_3, scale4_5, bias4_5, scale6_7,
                                         bias6_7, scale8_9, bias8_9, scale10_11, bias10_11,
                                         scale12_13, bias12_13, scale14_15, bias14_15);
  }

  gradColor.rgb *= gradColor.a;
  gradColor *= outputColor.a;

#if HAS_VCOVERAGE
#define TGFX_COVERAGE_SRC_COLOR (gradColor * vCoverage)
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_COVERAGE_SRC_COLOR gradColor
#endif
#include "coverage_output.inc"
#include "xp_output.inc"
}
