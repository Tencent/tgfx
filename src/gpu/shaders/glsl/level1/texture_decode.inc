// Shared texture-sample decode math for the plain color-leaf consumers (pilot: TextureFillShader
// and TextureColorMatrixShader, whose decode blocks were byte-identical copies). The functions
// carry the math only: samplers, coordinate computation, and the conditional alpha-plane fetch
// stay at the call site, so the sampler binding and control flow of each family are preserved.
// The alpha-only *color* decode here must not be reused for glyph/mask coverage semantics.
#ifndef TGFX_TEXTURE_DECODE_INC
#define TGFX_TEXTURE_DECODE_INC

// Alpha-only textures use R8 format in Metal. The sampler returns (r, 0, 0, 1); replicating .r
// puts the actual alpha value in every channel.
vec4 tdDecodeSample(vec4 color, int alphaOnly) {
  if (alphaOnly != 0) {
    color = vec4(color.r);
  }
  return color;
}

// RGBAAA dual-plane: modulate the rgb channels by the separately sampled alpha plane and replace
// alpha. Both inputs are clamped exactly as the previous inline copies did.
vec4 tdApplyRgbaaa(vec4 color, vec4 alphaSample) {
  color = clamp(color, 0.0, 1.0);
  alphaSample = clamp(alphaSample, 0.0, 1.0);
  return vec4(color.rgb * alphaSample.r, alphaSample.r);
}

// Paint-color modulation: an alpha-only sample modulates the paint color by its alpha; an rgba
// sample is modulated by the paint color's alpha.
vec4 tdModulateByPaint(vec4 color, vec4 paintColor, int alphaOnly) {
  if (alphaOnly != 0) {
    return color.a * paintColor;
  }
  return color * paintColor.a;
}

#endif  // TGFX_TEXTURE_DECODE_INC
