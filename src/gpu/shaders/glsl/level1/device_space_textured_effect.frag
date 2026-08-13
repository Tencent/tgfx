// DeviceSpaceTexturedEffectShader fragment shader
// Processor layout: ComposeFragmentProcessor(DeviceSpaceTextureEffect, pointwise op).
// Permutation dimensions (frag): HAS_XP, HAS_COVERAGE.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  mat3 DeviceCoordMatrix;
#include "pointwise_op_uniforms.inc"
#include "xp_uniforms.inc"
  int AlphaOnly;
};

#if HAS_COVERAGE
layout(location = 0) in float vCoverage;
#endif

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

#include "pointwise_op.inc"

void main() {
  highp vec3 deviceCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  vec4 color = texture(TextureSampler_0, deviceCoord.xy);

  if (AlphaOnly != 0) {
    color = vec4(color.r);
    color = color.a * Color;
  } else {
    color = color * Color.a;
  }

  vec4 result = applyPointwiseOp(0, color);

#if HAS_COVERAGE
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
