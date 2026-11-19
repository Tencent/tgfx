//@function chainTail_porterDuff
vec4 chainTail_porterDuff(vec4 color, vec4 finalCoverage) {
  return applyPorterDuffXP(color, finalCoverage, gl_FragCoord.xy, DstTextureUpperLeft,
                           DstTextureCoordScale, XPBlendMode, DstTextureSampler);
}
