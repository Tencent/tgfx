//@function tiledApplyBorder
// The ClampToBorder post-pass, parameterized for the assembled text: the interpreter's global
// ShaderModeX/Y and Subset ride the call.
vec4 tiledApplyBorder(vec4 color, int modeX, int modeY, vec4 subsetRect, vec2 inCoord,
                      vec2 subsetCoord, vec2 clampedCoord) {
  if (modeX == 8) {
    float errX = subsetCoord.x - clampedCoord.x;
    color = mix(color, vec4(0.0), min(abs(errX), 1.0));
  }
  if (modeY == 8) {
    float errY = subsetCoord.y - clampedCoord.y;
    color = mix(color, vec4(0.0), min(abs(errY), 1.0));
  }
  if (modeX == 7) {
    float snappedX = floor(inCoord.x + 0.001) + 0.5;
    if (snappedX < subsetRect.x || snappedX > subsetRect.z) {
      color = vec4(0.0);
    }
  }
  if (modeY == 7) {
    float snappedY = floor(inCoord.y + 0.001) + 0.5;
    if (snappedY < subsetRect.y || snappedY > subsetRect.w) {
      color = vec4(0.0);
    }
  }
  return color;
}
