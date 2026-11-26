//@function xpBlend_SrcIn
//@op XpBlendSrcIn
//@frag
vec4 xpBlend_SrcIn(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result = clamp(S * D.a, 0.0, 1.0);

  return result;
}
