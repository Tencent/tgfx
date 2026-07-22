// TiledTextureFillShader fragment shader
// Permutation dimensions (frag): ALPHA_ONLY, HAS_STRICT, HAS_XP
// ShaderModeX/ShaderModeY are runtime uniforms. Supported values (others fall back):
//   0=None, 1=Clamp, 2=RepeatNearestNone, 6=MirrorRepeat,
//   7=ClampToBorderNearest, 8=ClampToBorderLinear
// The tiling math is shared with the blur kernel via tiled_sample.inc and mirrors the runtime
// GLSLTiledTextureEffect codegen (Dimension normalization for the unnormalized ClampToBorder modes).
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_STRICT
#define HAS_STRICT 0
#endif

#if HAS_PERSPECTIVE
layout(location = 0) in vec3 TransformedCoords_0;
#else
layout(location = 0) in vec2 TransformedCoords_0;
#endif

layout(location = 0) out vec4 fragColor;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int ShaderModeX;
  int ShaderModeY;
  vec4 Subset;
  vec4 Clamp;
  vec2 Dimension;
#include "xp_uniforms.inc"
};

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "tiled_sample.inc"

void main() {
#if HAS_PERSPECTIVE
  vec2 texCoord = TransformedCoords_0.xy / TransformedCoords_0.z;
#else
  vec2 texCoord = TransformedCoords_0;
#endif

  vec2 inCoord;
  vec2 subsetCoord;
  vec2 clampedCoord;
  vec2 sampleCoord = tiledMapCoord(texCoord, HAS_STRICT != 0, inCoord, subsetCoord, clampedCoord);
  vec4 color = texture(TextureSampler_0, sampleCoord);
  color = tiledApplyBorder(color, inCoord, subsetCoord, clampedCoord);

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
