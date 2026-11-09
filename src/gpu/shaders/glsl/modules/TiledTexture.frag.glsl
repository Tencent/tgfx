// TiledTexture fragment hook: the single-tap tiled sample modulated by the input color's alpha.
// The expressions are verbatim from the former fill_fragment_main.inc step FILL_SOURCE_TILED; the
// alias defines below map the shared tiled_sample.inc contract onto this module's field names.
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
