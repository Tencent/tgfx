// Pure pointwise fragment: the constant color's input modes. 0 = Ignore (replace), 1 =
// ModulateRGBA, 2 = ModulateA. See ops/ColorMatrix.glsl for the rules.
vec4 pmApplyConstColor(vec4 constColor, vec4 inputColor, int inputMode) {
  if (inputMode == 1) {
    return constColor * inputColor;
  }
  if (inputMode == 2) {
    return constColor * inputColor.a;
  }
  return constColor;
}
