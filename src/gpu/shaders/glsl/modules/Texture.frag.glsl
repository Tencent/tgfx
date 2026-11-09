// Texture fragment hook: the plain leaf read (the former FILL_SOURCE_TEXTURE). The expressions
// are verbatim from the former fill pipeline's source stage.
#define TextureSampler_0 Texture_Sampler
#define Subset Texture_Subset
#define SubsetClampOn Texture_SubsetClampOn
#define AlphaStart Texture_AlphaStart
#define AlphaOnly Texture_AlphaOnly
#define HasRgbaaa Texture_HasRgbaaa
#include "source/texture_sampling.inc"
#undef TextureSampler_0
#undef Subset
#undef SubsetClampOn
#undef AlphaStart
#undef AlphaOnly
#undef HasRgbaaa

void Texture_fragment(inout Fragment f) {
  highp vec2 finalCoord = Texture_vCoord.xy / Texture_vCoord.z;
#if Texture_HAS_SUBSET
  finalCoord = aotTextureClamp(finalCoord, Texture_vTexSubset, true);
#endif
  f.color = aotTextureSource(Texture_Sampler, finalCoord, f.color, Texture_Subset,
                             Texture_SubsetClampOn, Texture_AlphaStart, Texture_AlphaOnly,
                             Texture_HasRgbaaa, 0);
}
