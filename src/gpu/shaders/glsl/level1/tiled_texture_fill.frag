// TiledTextureFillShader fragment shader
// Permutation dimensions (frag): ALPHA_ONLY, HAS_STRICT, HAS_XP
// ShaderModeX/ShaderModeY are runtime uniforms. Supported values (others fall back):
//   0=None, 1=Clamp, 2=RepeatNearestNone, 6=MirrorRepeat,
//   7=ClampToBorderNearest, 8=ClampToBorderLinear
// This mirrors the runtime GLSLTiledTextureEffect codegen so the precompiled variant is numerically
// equivalent. Modes 7 and 8 (ClampToBorder) require unnormalized coordinates: the incoming coord is
// in texture-pixel space and is divided by the Dimension uniform before tiling, then multiplied back
// when sampling. The Subset/Clamp uniforms are uploaded in that same (pixel) space for these modes,
// and in normalized space otherwise.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
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

// A shader mode needs unnormalized (pixel-space) coordinates when it is ClampToBorder. The
// precompiled variant does not support RepeatLinear/mipmap modes (3,4,5); those fall back.
bool tiledUsesUnorm(int mode) {
  return mode == 7 || mode == 8;
}

// A shader mode clamps the tiled coordinate to the Clamp rect. Matches ShaderModeUsesClamp():
// Clamp, RepeatNearest, MirrorRepeat and ClampToBorderLinear clamp; None and ClampToBorderNearest do
// not.
bool tiledUsesClamp(int mode) {
  return mode == 1 || mode == 2 || mode == 6 || mode == 8;
}

// Computes the pre-clamp "subset coordinate" for a single axis, matching the runtime subsetCoord().
float tiledSubsetCoord(float coord, float subLo, float subHi, int mode) {
  if (mode == 2) {
    // RepeatNearestNone: wrap into [subLo, subHi)
    return mod(coord - subLo, subHi - subLo) + subLo;
  }
  if (mode == 6) {
    // MirrorRepeat
    float w = subHi - subLo;
    float m = mod(coord - subLo, 2.0 * w);
    return mix(m, 2.0 * w - m, step(w, m)) + subLo;
  }
  // None, Clamp, ClampToBorderNearest, ClampToBorderLinear: unchanged
  return coord;
}

void main() {
#if HAS_PERSPECTIVE
  vec2 texCoord = TransformedCoords_0.xy / TransformedCoords_0.z;
#else
  vec2 texCoord = TransformedCoords_0;
#endif

  bool unorm = tiledUsesUnorm(ShaderModeX) || tiledUsesUnorm(ShaderModeY);
  vec2 inCoord = texCoord;
  if (unorm) {
    inCoord /= Dimension;
  }

  vec2 subsetCoord;
  subsetCoord.x = tiledSubsetCoord(inCoord.x, Subset.x, Subset.z, ShaderModeX);
  subsetCoord.y = tiledSubsetCoord(inCoord.y, Subset.y, Subset.w, ShaderModeY);

  vec2 clampedCoord;
  clampedCoord.x =
      tiledUsesClamp(ShaderModeX) ? clamp(subsetCoord.x, Clamp.x, Clamp.z) : subsetCoord.x;
  clampedCoord.y =
      tiledUsesClamp(ShaderModeY) ? clamp(subsetCoord.y, Clamp.y, Clamp.w) : subsetCoord.y;

#if HAS_STRICT
  clampedCoord = clamp(clampedCoord, Clamp.xy, Clamp.zw);
#endif

  vec2 sampleCoord = unorm ? clampedCoord * Dimension : clampedCoord;
  vec4 color = texture(TextureSampler_0, sampleCoord);

  // ClampToBorderLinear: fade to transparent by the clamp error, matching the runtime.
  if (ShaderModeX == 8) {
    float errX = subsetCoord.x - clampedCoord.x;
    color = mix(color, vec4(0.0), min(abs(errX), 1.0));
  }
  if (ShaderModeY == 8) {
    float errY = subsetCoord.y - clampedCoord.y;
    color = mix(color, vec4(0.0), min(abs(errY), 1.0));
  }
  // ClampToBorderNearest: snap to texel center and discard outside the subset, matching the runtime.
  if (ShaderModeX == 7) {
    float snappedX = floor(inCoord.x + 0.001) + 0.5;
    if (snappedX < Subset.x || snappedX > Subset.z) {
      color = vec4(0.0);
    }
  }
  if (ShaderModeY == 7) {
    float snappedY = floor(inCoord.y + 0.001) + 0.5;
    if (snappedY < Subset.y || snappedY > Subset.w) {
      color = vec4(0.0);
    }
  }

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
