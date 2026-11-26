//@function xpBlend_DstATop
//@op XpBlendDstATop
//@frag
vec4 xpBlend_DstATop(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result = clamp(S * (1.0 - D.a) + D * S.a, 0.0, 1.0);

  return result;
}
