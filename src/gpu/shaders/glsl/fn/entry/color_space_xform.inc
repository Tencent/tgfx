//@function chainApplyColorSpaceXform
//@member-start 1
//@members Chain
//@member flags ChainCSFlags
//@source flags CSFlags
//@member srcTF0 ChainSrcTF0
//@source srcTF0 CSTransferSrc0
//@member srcTF1 ChainSrcTF1
//@source srcTF1 CSTransferSrc1
//@member srcTFType ChainSrcTFType
//@source srcTFType CSTransferSrcType
//@member srcOOTF ChainSrcOOTF
//@source srcOOTF CSOotfSrc
//@member colorXform ChainColorXform
//@source colorXform CSColorXform
//@member dstOOTF ChainDstOOTF
//@source dstOOTF CSOotfDst
//@member dstTF0 ChainDstTF0
//@source dstTF0 CSTransferDst0
//@member dstTF1 ChainDstTF1
//@source dstTF1 CSTransferDst1
//@member dstTFType ChainDstTFType
//@source dstTFType CSTransferDstType
vec4 chainApplyColorSpaceXform(vec4 color, int flags, vec4 srcTF0, vec4 srcTF1, int srcTFType,
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
