// The advanced blend pass's composite: the loop's registers carry the blended operands and the
// coverage discard mirrors applyPorterDuffXP's. The mode rides the XPBlendMode uniform; only the
// advanced modes (>= XP_FIRST_ADVANCED_MODE) route here, and every one of them has a coverage
// form, so the dispatch below never falls back.
//@function xpCompositeBlendPass
vec4 xpCompositeBlendPass(vec4 S, vec4 D, vec4 C, int mode) {
  if (C.r <= 0.0 && C.g <= 0.0 && C.b <= 0.0) {
    discard;
  }
  return xpBlendWithCoverage(S, D, C, mode);
}
