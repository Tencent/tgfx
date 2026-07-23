// TiledTextureFillShader fragment shader
// Permutation dimensions (frag): HAS_XP
// ShaderModeX/ShaderModeY are runtime uniforms. Supported values (others fall back):
//   0=None, 1=Clamp, 2=RepeatNearestNone, 6=MirrorRepeat,
//   7=ClampToBorderNearest, 8=ClampToBorderLinear
// AlphaOnly (alpha-only source) and Strict (SrcRectConstraint::Strict) are runtime uniforms rather
// than compile-time permutations: both are pure fragment math, so folding them into coherent
// uniform branches shrinks the variant count (mirrors QuadTextureFillShader).
// The coordinate is always affine (vec2); perspective TiledTextureEffect draws fall back to
// ProgramBuilder, so no perspective path exists here.
// The tiling math is shared with the blur kernel via tiled_sample.inc and mirrors the runtime
// GLSLTiledTextureEffect codegen (Dimension normalization for the unnormalized ClampToBorder modes).
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(location = 0) in vec2 TransformedCoords_0;

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
  // Alpha-only source (R8 texture) and strict-subset clamping are runtime uniforms rather than
  // compile-time permutations; both are pure fragment math (coherent branches).
  int AlphaOnly;
  int Strict;
};

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "tiled_sample.inc"

void main() {
  vec2 texCoord = TransformedCoords_0;

  vec2 inCoord;
  vec2 subsetCoord;
  vec2 clampedCoord;
  vec2 sampleCoord = tiledMapCoord(texCoord, Strict != 0, inCoord, subsetCoord, clampedCoord);
  vec4 color = texture(TextureSampler_0, sampleCoord);
  color = tiledApplyBorder(color, inCoord, subsetCoord, clampedCoord);

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
