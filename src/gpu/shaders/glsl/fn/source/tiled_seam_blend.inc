//@function tiledSeamBlend
// The RepeatLinearNone seam blend, parameterized for the assembled text: the leaf sampler rides the
// call (the assembly names its own TextureSampler_N statically) and the recipe arrives as arguments
// instead of the interpreter's globals. The math matches source/texture_sampling.inc's tiled path.
vec4 tiledSeamBlend(sampler2D texSampler, vec4 texColor, int modeX, int modeY, vec4 clampRect,
                    vec2 dimension, vec2 inCoord, vec2 subsetCoord, vec2 clampedCoord,
                    int readSwizzle) {
  bool repeatX = modeX == 3 && subsetCoord.x != clampedCoord.x;
  bool repeatY = modeY == 3 && subsetCoord.y != clampedCoord.y;
  if (!repeatX && !repeatY) {
    return texColor;
  }
  float errX = subsetCoord.x - clampedCoord.x;
  float errY = subsetCoord.y - clampedCoord.y;
  float repeatCoordX = errX > 0.0 ? clampRect.x : clampRect.z;
  float repeatCoordY = errY > 0.0 ? clampRect.y : clampRect.w;
  if (repeatX && repeatY) {
    vec4 repeatReadX =
        tgfxTextureSample(texSampler, vec2(repeatCoordX, clampedCoord.y) * dimension, vec4(0.0), 0, readSwizzle);
    vec4 repeatReadY =
        tgfxTextureSample(texSampler, vec2(clampedCoord.x, repeatCoordY) * dimension, vec4(0.0), 0, readSwizzle);
    vec4 repeatReadXY =
        tgfxTextureSample(texSampler, vec2(repeatCoordX, repeatCoordY) * dimension, vec4(0.0), 0, readSwizzle);
    return mix(mix(texColor, repeatReadX, abs(errX)), mix(repeatReadY, repeatReadXY, abs(errX)),
               abs(errY));
  }
  if (repeatX) {
    vec4 repeatReadX =
        tgfxTextureSample(texSampler, vec2(repeatCoordX, clampedCoord.y) * dimension, vec4(0.0), 0, readSwizzle);
    return mix(texColor, repeatReadX, errX);
  }
  vec4 repeatReadY =
      tgfxTextureSample(texSampler, vec2(clampedCoord.x, repeatCoordY) * dimension, vec4(0.0), 0, readSwizzle);
  return mix(texColor, repeatReadY, errY);
}
