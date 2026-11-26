//@function xpBlend_DstIn
//@op XpBlendDstIn
//@frag
vec4 xpBlend_DstIn(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result = clamp(D * S.a, 0.0, 1.0);

  return result;
}
