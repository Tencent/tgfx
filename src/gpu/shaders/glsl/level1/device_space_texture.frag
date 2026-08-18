// DeviceSpaceTextureShader fragment shader
// Permutation dimensions (frag): HAS_XP
// ALPHA_ONLY is a runtime uniform (AlphaOnly), written by GLSLDeviceSpaceTextureEffect::onSetData,
// not a compile-time permutation: it is pure fragment math, so folding it into a uniform branch
// shrinks the variant count.
// Samples texture using device-space (screen) coordinates via gl_FragCoord.
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
#include "xp_uniforms.inc"
  int AlphaOnly;
};

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

#if HAS_COVERAGE
layout(location = 0) in float vCoverage;
#endif

layout(location = 0) out vec4 fragColor;

void main() {
  highp vec3 deviceCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  vec4 color = texture(TextureSampler_0, deviceCoord.xy);
#if HAS_COVERAGE
  // The runtime composites the geometry AA coverage into the sampled device-space coverage
  // (texture.r * vCoverage) before the paint color modulation.
  color *= vCoverage;
#endif

  if (AlphaOnly != 0) {
    // Alpha-only textures use R8 format in Metal. Use .r to get the actual alpha value.
    color = vec4(color.r);
    color = color.a * Color;
  } else {
    color = color * Color.a;
  }

#define TGFX_XP_SRC_COLOR color
#include "xp_output.inc"
}
