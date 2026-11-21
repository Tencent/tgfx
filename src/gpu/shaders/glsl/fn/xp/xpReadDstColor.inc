//@function xpReadDstColor
vec4 xpReadDstColor() {
  highp vec2 dstCoord = (gl_FragCoord.xy - DstTextureUpperLeft) * DstTextureCoordScale;
  return texture(DstTextureSampler, dstCoord);
}
