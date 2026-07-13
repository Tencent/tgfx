// TiledTextureFillShader fragment shader
// Permutation dimensions (frag): SHADER_MODE_X(0-8), SHADER_MODE_Y(0-8), ALPHA_ONLY, HAS_STRICT, HAS_XP
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
#if SHADER_MODE_X >= 2 || SHADER_MODE_Y >= 2
  vec4 Subset;
#endif
#if SHADER_MODE_X >= 1 || SHADER_MODE_Y >= 1
  vec4 Clamp;
#endif
#include "xp_uniforms.inc"
};

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"

// Tile a coordinate for repeat mode: result in [subset.lo, subset.hi)
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

void main() {
#if HAS_PERSPECTIVE
  vec2 texCoord = TransformedCoords_0.xy / TransformedCoords_0.z;
#else
  vec2 texCoord = TransformedCoords_0;
#endif

  vec2 coord = texCoord;

  // --- X axis tiling ---
#if SHADER_MODE_X == 1
  // Clamp
  coord.x = clamp(coord.x, Clamp.x, Clamp.z);
#elif SHADER_MODE_X == 2
  // RepeatNearestNone
  coord.x = tileRepeat(coord.x, Subset.x, Subset.z);
  coord.x = clamp(coord.x, Clamp.x, Clamp.z);
#elif SHADER_MODE_X == 3
  // RepeatLinearNone
  coord.x = tileRepeat(coord.x, Subset.x, Subset.z);
  coord.x = clamp(coord.x, Clamp.x, Clamp.z);
#elif SHADER_MODE_X == 4
  // RepeatLinearMipmap
  coord.x = tileRepeat(coord.x, Subset.x, Subset.z);
  coord.x = clamp(coord.x, Clamp.x, Clamp.z);
#elif SHADER_MODE_X == 5
  // RepeatNearestMipmap
  coord.x = tileRepeat(coord.x, Subset.x, Subset.z);
  coord.x = clamp(coord.x, Clamp.x, Clamp.z);
#elif SHADER_MODE_X == 6
  // MirrorRepeat
  coord.x = tileMirror(coord.x, Subset.x, Subset.z);
  coord.x = clamp(coord.x, Clamp.x, Clamp.z);
#elif SHADER_MODE_X == 7
  // ClampToBorderNearest
  // handled after sampling
#elif SHADER_MODE_X == 8
  // ClampToBorderLinear
  coord.x = clamp(coord.x, Clamp.x, Clamp.z);
#endif

  // --- Y axis tiling ---
#if SHADER_MODE_Y == 1
  coord.y = clamp(coord.y, Clamp.y, Clamp.w);
#elif SHADER_MODE_Y == 2
  coord.y = tileRepeat(coord.y, Subset.y, Subset.w);
  coord.y = clamp(coord.y, Clamp.y, Clamp.w);
#elif SHADER_MODE_Y == 3
  coord.y = tileRepeat(coord.y, Subset.y, Subset.w);
  coord.y = clamp(coord.y, Clamp.y, Clamp.w);
#elif SHADER_MODE_Y == 4
  coord.y = tileRepeat(coord.y, Subset.y, Subset.w);
  coord.y = clamp(coord.y, Clamp.y, Clamp.w);
#elif SHADER_MODE_Y == 5
  coord.y = tileRepeat(coord.y, Subset.y, Subset.w);
  coord.y = clamp(coord.y, Clamp.y, Clamp.w);
#elif SHADER_MODE_Y == 6
  coord.y = tileMirror(coord.y, Subset.y, Subset.w);
  coord.y = clamp(coord.y, Clamp.y, Clamp.w);
#elif SHADER_MODE_Y == 7
  // ClampToBorderNearest - handled after sampling
#elif SHADER_MODE_Y == 8
  coord.y = clamp(coord.y, Clamp.y, Clamp.w);
#endif

#if HAS_STRICT
  // Additional clamp for strict constraint
  coord = clamp(coord, Clamp.xy, Clamp.zw);
#endif

  // --- Sample ---
  vec4 color = texture(TextureSampler_0, coord);

  // --- ClampToBorder post-processing ---
#if SHADER_MODE_X == 7
  // ClampToBorderNearest X: if outside subset, output transparent
  if (texCoord.x < Subset.x || texCoord.x >= Subset.z) {
    color = vec4(0.0);
  }
#endif
#if SHADER_MODE_Y == 7
  // ClampToBorderNearest Y
  if (texCoord.y < Subset.y || texCoord.y >= Subset.w) {
    color = vec4(0.0);
  }
#endif
#if SHADER_MODE_X == 8
  // ClampToBorderLinear X: fade at edges
  float fadeX = smoothstep(Subset.x - 0.5, Subset.x + 0.5, texCoord.x) *
                (1.0 - smoothstep(Subset.z - 0.5, Subset.z + 0.5, texCoord.x));
  color *= fadeX;
#endif
#if SHADER_MODE_Y == 8
  // ClampToBorderLinear Y: fade at edges
  float fadeY = smoothstep(Subset.y - 0.5, Subset.y + 0.5, texCoord.y) *
                (1.0 - smoothstep(Subset.w - 0.5, Subset.w + 0.5, texCoord.y));
  color *= fadeY;
#endif

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
