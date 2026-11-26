//@function xpBlend_DstOver
//@op XpBlendDstOver
//@frag
vec4 xpBlend_DstOver(vec4 S /*@in*/, vec4 D /*@in*/, vec4 dstSrcAlpha /*@in*/, vec4 srcDstAlpha /*@in*/) {
  vec4 result;
  result = clamp(S * (1.0 - D.a) + D, 0.0, 1.0);

  return result;
}
