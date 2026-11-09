// TiledTexture fragment hook. FULL = 0 is the single-tap recipe of the former
// fill_fragment_main.inc step FILL_SOURCE_TILED; FULL = 1 is the full nine-mode recipe of the
// former kernels/tiled_texture_fill.frag (FILL_SOURCE_TILED_FULL), including the mipmap-repeat
// 4-tap seam blend and the runtime read swizzle. Both aliases sets map the shared
// tiled_sample.inc / tiled_full_sample.inc contract onto this module's field names.
#if TiledTexture_FULL
#define ShaderModeX TiledTexture_ShaderModeX
#define ShaderModeY TiledTexture_ShaderModeY
#define Subset TiledTexture_Subset
#define Clamp TiledTexture_Clamp
#define Dimension TiledTexture_Dimension
#define AlphaOnly TiledTexture_AlphaOnly
#define Strict TiledTexture_Strict
#define ReadSwizzle TiledTexture_ReadSwizzle
#define TextureSampler_0 TiledTexture_Sampler
#include "source/tiled_sample.inc"
#include "source/texture_read_swizzle.inc"
#include "source/tiled_full_sample.inc"
#undef ShaderModeX
#undef ShaderModeY
#undef Subset
#undef Clamp
#undef Dimension
#undef AlphaOnly
#undef Strict
#undef ReadSwizzle
#undef TextureSampler_0

void TiledTexture_fragment(inout Fragment f) {
  f.color = aotTiledFullSample(TiledTexture_vCoord, f.color);
}
#else
#define ShaderModeX TiledTexture_ShaderModeX
#define ShaderModeY TiledTexture_ShaderModeY
#define Subset TiledTexture_Subset
#define Clamp TiledTexture_Clamp
#define Dimension TiledTexture_Dimension
#include "source/tiled_sample.inc"
#undef ShaderModeX
#undef ShaderModeY
#undef Subset
#undef Clamp
#undef Dimension

void TiledTexture_fragment(inout Fragment f) {
  vec2 texCoord = TiledTexture_vCoord;
  vec2 inCoord;
  vec2 subsetCoord;
  vec2 clampedCoord;
  vec2 sampleCoord = tiledMapCoord(texCoord, TiledTexture_Strict != 0, inCoord, subsetCoord,
                                   clampedCoord);
  vec4 texColor = texture(TiledTexture_Sampler, sampleCoord);
  texColor = tiledApplyBorder(texColor, inCoord, subsetCoord, clampedCoord);
  if (TiledTexture_AlphaOnly != 0) {
    texColor = vec4(texColor.r);
    f.color = texColor.a * f.color;
  } else {
    f.color = texColor * f.color.a;
  }
}
#endif
