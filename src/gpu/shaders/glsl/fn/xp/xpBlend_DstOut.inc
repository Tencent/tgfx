//@function xpBlend_DstOut
//@op XpBlendDstOut
//@frag
vec4 xpBlend_DstOut(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result = clamp(D * (1.0 - S.a), 0.0, 1.0);

  return result;
}
