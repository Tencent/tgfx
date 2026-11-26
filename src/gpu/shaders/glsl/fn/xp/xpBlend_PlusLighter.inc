//@function xpBlend_PlusLighter
//@op XpBlendPlusLighter
//@frag
vec4 xpBlend_PlusLighter(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result = clamp(S + D, 0.0, 1.0);

  return result;
}
