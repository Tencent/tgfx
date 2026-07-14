// TiledTextureFillShader fragment shader
// Permutation dimensions (frag): ALPHA_ONLY, HAS_STRICT, HAS_XP
// ShaderModeX/ShaderModeY are runtime uniforms (0-8).
// ShaderMode values: 0=None, 1=Clamp, 2=RepeatNearestNone, 3=RepeatLinearNone,
//   4=RepeatLinearMipmap, 5=RepeatNearestMipmap, 6=MirrorRepeat,
//   7=ClampToBorderNearest, 8=ClampToBorderLinear
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
#include "xp_uniforms.inc"
};

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

// Tile a coordinate for repeat mode: result in [lo, hi)
float tileRepeat(float coord, float lo, float hi) {
  float len = hi - lo;
  return mod(coord - lo, len) + lo;
}

// Tile a coordinate for mirror-repeat mode
float tileMirror(float coord, float lo, float hi) {
  float len = hi - lo;
  float t = mod(coord - lo, 2.0 * len);
  return mix(lo + t, hi - (t - len), step(len, t));
}

// Apply tiling for a single axis based on runtime ShaderMode uniform
float tileAxis(float coord, float subLo, float subHi, float clampLo, float clampHi, int mode) {
  if (mode == 1) {
    // Clamp
    return clamp(coord, clampLo, clampHi);
  } else if (mode >= 2 && mode <= 5) {
    // RepeatNearestNone / RepeatLinearNone / RepeatLinearMipmap / RepeatNearestMipmap
    coord = tileRepeat(coord, subLo, subHi);
    return clamp(coord, clampLo, clampHi);
  } else if (mode == 6) {
    // MirrorRepeat
    coord = tileMirror(coord, subLo, subHi);
    return clamp(coord, clampLo, clampHi);
  } else if (mode == 8) {
    // ClampToBorderLinear
    return clamp(coord, clampLo, clampHi);
  }
  // mode == 0 (None) or mode == 7 (ClampToBorderNearest): no pre-sample coord change
  return coord;
}

void main() {
#if HAS_PERSPECTIVE
  vec2 texCoord = TransformedCoords_0.xy / TransformedCoords_0.z;
#else
  vec2 texCoord = TransformedCoords_0;
#endif

  vec2 coord = texCoord;

  // --- Axis tiling via runtime uniform branch ---
  coord.x = tileAxis(coord.x, Subset.x, Subset.z, Clamp.x, Clamp.z, ShaderModeX);
  coord.y = tileAxis(coord.y, Subset.y, Subset.w, Clamp.y, Clamp.w, ShaderModeY);

#if HAS_STRICT
  // Additional clamp for strict constraint
  coord = clamp(coord, Clamp.xy, Clamp.zw);
#endif

  // --- Sample ---
  vec4 color = texture(TextureSampler_0, coord);

  // --- ClampToBorder post-processing (runtime branch) ---
  if (ShaderModeX == 7) {
    // ClampToBorderNearest X: if outside subset, output transparent
    if (texCoord.x < Subset.x || texCoord.x >= Subset.z) {
      color = vec4(0.0);
    }
  }
  if (ShaderModeY == 7) {
    // ClampToBorderNearest Y
    if (texCoord.y < Subset.y || texCoord.y >= Subset.w) {
      color = vec4(0.0);
    }
  }
  if (ShaderModeX == 8) {
    // ClampToBorderLinear X: fade at edges
    float fadeX = smoothstep(Subset.x - 0.5, Subset.x + 0.5, texCoord.x) *
                  (1.0 - smoothstep(Subset.z - 0.5, Subset.z + 0.5, texCoord.x));
    color *= fadeX;
  }
  if (ShaderModeY == 8) {
    // ClampToBorderLinear Y: fade at edges
    float fadeY = smoothstep(Subset.y - 0.5, Subset.y + 0.5, texCoord.y) *
                  (1.0 - smoothstep(Subset.w - 0.5, Subset.w + 0.5, texCoord.y));
    color *= fadeY;
  }

  // --- Alpha-only and output ---
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
