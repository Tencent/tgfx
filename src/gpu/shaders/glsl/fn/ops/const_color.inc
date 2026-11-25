//@function pmApplyConstColor
//@op ConstColor fragment
vec4 pmApplyConstColor(vec4 constColor, vec4 inputColor /*@in*/, int inputMode /*@in*/) {
  if (inputMode == 1) {
    return constColor * inputColor;
  }
  if (inputMode == 2) {
    return constColor * inputColor.a;
  }
  return constColor;
}
