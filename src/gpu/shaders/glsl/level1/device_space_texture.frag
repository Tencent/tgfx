// DeviceSpaceTextureShader fragment shader
// Permutation dimensions (frag): ALPHA_ONLY, HAS_XP
// Samples texture using device-space (screen) coordinates via gl_FragCoord.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  mat3 DeviceCoordMatrix;
#include "xp_uniforms.inc"
};

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  highp vec3 deviceCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  vec4 color = texture(TextureSampler_0, deviceCoord.xy);

#if ALPHA_ONLY
  // Alpha-only textures use R8 format in Metal. Use .r to get the actual alpha value.
  color = vec4(color.r);
  color = color.a * Color;
#else
  color = color * Color.a;
#endif

#define TGFX_XP_SRC_COLOR color
#include "xp_output.inc"
}
