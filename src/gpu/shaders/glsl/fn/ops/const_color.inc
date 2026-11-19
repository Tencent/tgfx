//@function pmApplyConstColor
vec4 pmApplyConstColor(vec4 constColor, vec4 inputColor, int inputMode) {
  if (inputMode == 1) {
    return constColor * inputColor;
  }
  if (inputMode == 2) {
    return constColor * inputColor.a;
  }
  return constColor;
}
