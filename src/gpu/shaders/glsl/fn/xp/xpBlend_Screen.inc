//@function xpBlend_Screen
//@op XpBlendScreen
//@frag
vec4 xpBlend_Screen(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result = clamp(S + D - S * D, 0.0, 1.0);

  return result;
}
