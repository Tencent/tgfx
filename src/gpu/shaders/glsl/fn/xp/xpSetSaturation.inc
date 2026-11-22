//@function xpSetSaturation
vec3 xpSetSaturation(vec3 hueLumColor, vec3 satColor) {
  // Component-wise rank form of the classic min/mid/max permutation tree: every channel maps to
  // sat * (c - min) / (max - min), which is exactly the tree's mid-channel expression, while the
  // min channel evaluates to 0.0 and the max channel is replaced with the constant sat the tree
  // assigns to it. When max == min the tree returns all zeros; the sanitized denominator makes
  // every lane compute sat * 0 = 0 instead.
  float sat = xpSaturation(satColor);
  float minComp = min(min(hueLumColor.r, hueLumColor.g), hueLumColor.b);
  float maxComp = max(max(hueLumColor.r, hueLumColor.g), hueLumColor.b);
  float denom = (maxComp > minComp) ? (maxComp - minComp) : 1.0;
  vec3 ranked = (sat * (hueLumColor - vec3(minComp))) / vec3(denom);
  return mix(ranked, vec3(sat), equal(hueLumColor, vec3(maxComp)));
}
