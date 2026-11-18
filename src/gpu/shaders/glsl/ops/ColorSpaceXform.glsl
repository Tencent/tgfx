// Pure color-space transform fragment: the CS stage flags, the apply macro and the wrapper the
// chain's slot calls. The parameters arrive as uniform-block members (the transform's block is
// program-level, declared by the walk), and the transfer functions resolve through the shared
// transfer module — the same bytes serve the interpreter's include and the pack's expanded copy.
#ifndef TGFX_OPS_COLOR_SPACE_XFORM_INCLUDED
#define TGFX_OPS_COLOR_SPACE_XFORM_INCLUDED

#include "operator/transfer_function.inc"

// The chain slot's call form: the stages run in order on the program-level transform block, the
// same sequence the former apply macro spelled.
vec4 chainApplyColorSpaceXform(vec4 color) {
  if ((ChainCSFlags & 1) != 0) {
    float csAlpha = color.a;
    color = (csAlpha > 0.0) ? vec4(color.rgb / csAlpha, csAlpha) : vec4(0.0);
  }
  if ((ChainCSFlags & 2) != 0) {
    color.rgb = transferTF(color.rgb, ChainSrcTF0, ChainSrcTF1, ChainSrcTFType);
  }
  if ((ChainCSFlags & 32) != 0) {
    float csSrcY = dot(color.rgb, ChainSrcOOTF.rgb);
    color.rgb *= sign(csSrcY) * pow(abs(csSrcY), ChainSrcOOTF.a);
  }
  if ((ChainCSFlags & 4) != 0) {
    color.rgb = ChainColorXform * color.rgb;
  }
  if ((ChainCSFlags & 64) != 0) {
    float csDstY = dot(color.rgb, ChainDstOOTF.rgb);
    color.rgb *= sign(csDstY) * pow(abs(csDstY), ChainDstOOTF.a);
  }
  if ((ChainCSFlags & 8) != 0) {
    color.rgb = transferTF(color.rgb, ChainDstTF0, ChainDstTF1, ChainDstTFType);
  }
  if ((ChainCSFlags & 16) != 0) {
    color.rgb *= color.a;
  }
  return color;
}

#endif  // TGFX_OPS_COLOR_SPACE_XFORM_INCLUDED
