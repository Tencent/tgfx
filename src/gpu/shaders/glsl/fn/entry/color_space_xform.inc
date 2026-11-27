//@function chainApplyColorSpaceXform Chain
vec4 chainApplyColorSpaceXform(vec4 color /*@in*/, int flags, vec4 srcTF0, vec4 srcTF1, int srcTFType,
                               vec4 srcOOTF, mat3 colorXform, vec4 dstOOTF, vec4 dstTF0,
                               vec4 dstTF1, int dstTFType) {
  vec4 outColor = color;
  if ((flags & 1) != 0) {
    float csAlpha = outColor.a;
    outColor = (csAlpha > 0.0) ? vec4(outColor.rgb / csAlpha, csAlpha) : vec4(0.0);
  }
  if ((flags & 2) != 0) {
    outColor.rgb = transferTF(outColor.rgb, srcTF0, srcTF1, srcTFType);
  }
  if ((flags & 32) != 0) {
    float csSrcY = dot(outColor.rgb, srcOOTF.rgb);
    outColor.rgb *= sign(csSrcY) * pow(abs(csSrcY), srcOOTF.a);
  }
  if ((flags & 4) != 0) {
    outColor.rgb = colorXform * outColor.rgb;
  }
  if ((flags & 64) != 0) {
    float csDstY = dot(outColor.rgb, dstOOTF.rgb);
    outColor.rgb *= sign(csDstY) * pow(abs(csDstY), dstOOTF.a);
  }
  if ((flags & 8) != 0) {
    outColor.rgb = transferTF(outColor.rgb, dstTF0, dstTF1, dstTFType);
  }
  if ((flags & 16) != 0) {
    outColor.rgb *= outColor.a;
  }
  return outColor;
}
