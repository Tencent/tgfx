//@function chainApplyColorSpaceXform Chain
vec4 chainApplyColorSpaceXform(vec4 color /*@in*/, int flags, vec4 srcTF0, vec4 srcTF1, int srcTFType,
                               vec4 srcOOTF, mat3 colorXform, vec4 dstOOTF, vec4 dstTF0,
                               vec4 dstTF1, int dstTFType) {
  if ((flags & 1) != 0) {
    float csAlpha = color.a;
    color = (csAlpha > 0.0) ? vec4(color.rgb / csAlpha, csAlpha) : vec4(0.0);
  }
  if ((flags & 2) != 0) {
    color.rgb = transferTF(color.rgb, srcTF0, srcTF1, srcTFType);
  }
  if ((flags & 32) != 0) {
    float csSrcY = dot(color.rgb, srcOOTF.rgb);
    color.rgb *= sign(csSrcY) * pow(abs(csSrcY), srcOOTF.a);
  }
  if ((flags & 4) != 0) {
    color.rgb = colorXform * color.rgb;
  }
  if ((flags & 64) != 0) {
    float csDstY = dot(color.rgb, dstOOTF.rgb);
    color.rgb *= sign(csDstY) * pow(abs(csDstY), dstOOTF.a);
  }
  if ((flags & 8) != 0) {
    color.rgb = transferTF(color.rgb, dstTF0, dstTF1, dstTFType);
  }
  if ((flags & 16) != 0) {
    color.rgb *= color.a;
  }
  return color;
}
