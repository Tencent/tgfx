//@function tiledMapCoord
// The tiling coordinate map, parameterized for the assembled text: the interpreter's globals
// (ShaderModeX/Y, Subset, Clamp, Dimension) ride the call instead of a shared block, so one
// assembly serves every draw's own recipe.
vec2 tiledMapCoord(vec3 texCoord, bool strict, int modeX, int modeY, vec4 subsetRect,
                   vec4 clampRect, vec2 dimension, out vec2 inCoordOut, out vec2 subsetCoordOut,
                   out vec2 clampedCoordOut) {
  bool unormX = modeX == 3 || modeX == 4 || modeX == 5 || modeX == 7 || modeX == 8;
  bool unormY = modeY == 3 || modeY == 4 || modeY == 5 || modeY == 7 || modeY == 8;
  vec2 inCoord = texCoord.xy / texCoord.z;
  if (unormX || unormY) {
    inCoord /= dimension;
  }
  vec2 subsetCoord;
  if (modeX == 2 || modeX == 3) {
    subsetCoord.x = mod(inCoord.x - subsetRect.x, subsetRect.z - subsetRect.x) + subsetRect.x;
  } else if (modeX == 6) {
    float widthX = subsetRect.z - subsetRect.x;
    float mirroredX = mod(inCoord.x - subsetRect.x, 2.0 * widthX);
    subsetCoord.x = mix(mirroredX, 2.0 * widthX - mirroredX, step(widthX, mirroredX)) + subsetRect.x;
  } else {
    subsetCoord.x = inCoord.x;
  }
  if (modeY == 2 || modeY == 3) {
    subsetCoord.y = mod(inCoord.y - subsetRect.y, subsetRect.w - subsetRect.y) + subsetRect.y;
  } else if (modeY == 6) {
    float widthY = subsetRect.w - subsetRect.y;
    float mirroredY = mod(inCoord.y - subsetRect.y, 2.0 * widthY);
    subsetCoord.y = mix(mirroredY, 2.0 * widthY - mirroredY, step(widthY, mirroredY)) + subsetRect.y;
  } else {
    subsetCoord.y = inCoord.y;
  }
  bool clampX =
      modeX == 1 || modeX == 2 || modeX == 3 || modeX == 4 || modeX == 5 || modeX == 6 || modeX == 8;
  bool clampY =
      modeY == 1 || modeY == 2 || modeY == 3 || modeY == 4 || modeY == 5 || modeY == 6 || modeY == 8;
  vec2 clampedCoord;
  clampedCoord.x = clampX ? clamp(subsetCoord.x, clampRect.x, clampRect.z) : subsetCoord.x;
  clampedCoord.y = clampY ? clamp(subsetCoord.y, clampRect.y, clampRect.w) : subsetCoord.y;
  if (strict) {
    clampedCoord = clamp(clampedCoord, clampRect.xy, clampRect.zw);
  }
  inCoordOut = inCoord;
  subsetCoordOut = subsetCoord;
  clampedCoordOut = clampedCoord;
  return (unormX || unormY) ? clampedCoord * dimension : clampedCoord;
}
