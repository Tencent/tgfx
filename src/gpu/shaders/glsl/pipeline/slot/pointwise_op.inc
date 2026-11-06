// Shared pointwise-operator logic. Provides applyPointwiseOp(slot, ...), which applies one
// premultiplied-in/premultiplied-out pointwise operator read from a chain slot record:
// SlotPacked[slot].x selects the operator (0 = ColorMatrix, 1 = Luma, 2 = AlphaThreshold,
// 3 = ColorSpaceXform, 4 = None, 6 = ConstColor, 7 = Blend), the parameters are the slot's
// registers (SlotMat / SlotVec0..2; see pipeline/chain/pointwise_chain_uniforms.inc's allocation
// table), and the operator's small value rides the selector's low byte.
// OP_NONE lets a producer kernel (e.g. YUVTextureFillShader) emit its raw premultiplied output when
// no operator FP is composed on top of it, while the same slot record absorbs the composed case.
//
// One body serves every consumer: the chain interpreter calls it per slot, the YUV tail's loop
// calls it with the tail's slot index, and the single-operator TexturedEffect kernel calls it with
// slot 0. The FragmentUniformBlock must therefore declare the chain slot arrays (a capacity-1
// include for the single-operator kernel) and the shared ChainCS* block. The OP_* constants come
// from the shared op table; its guard makes the repeated per-slot inclusion of this file a no-op
// rather than a redefinition. This interpreter dispatches on the AOTPointwiseOpType subset; the
// chain-only defines in the table are simply unreferenced here.
#include "operator/op_codes.inc"

// OP_BLEND needs the shared PorterDuff blend math regardless of the kernel's XP state; the
// include is guarded, so the repeated inclusions of this file stay harmless.
#include "xp/xp_blend_colors.inc"
#include "operator/pointwise_math.inc"
#include "operator/pointwise_color_dispatch.inc"

// Applies the selected pointwise operator to a premultiplied color and returns a premultiplied color.
#if HAS_XP >= 1
// With xpPass set, this is instead the XferProcessor's advanced blend pass (xp_blend_pass.inc):
// it blends `color`, the XferProcessor source, with xpDst in XPBlendMode at the OP_BLEND site, so
// the kernel inlines xpBlendColors once. The caller keeps `slot` in range on that pass.
vec4 applyPointwiseOp(int slot, vec4 color, bool xpPass, vec4 xpDst) {
  int opType = xpPass ? OP_BLEND : SlotPacked[slot].x;
#else
vec4 applyPointwiseOp(int slot, vec4 color) {
  int opType = SlotPacked[slot].x;
#endif
  if (opType == OP_NONE) {
    return color;
  }
  vec4 result;
  if (opType == OP_COLOR_MATRIX || opType == OP_LUMA || opType == OP_ALPHA_THRESHOLD ||
      opType == OP_CONST_COLOR) {
    // The shared dispatch owns the operator-to-register mapping
    // (operator/pointwise_color_dispatch.inc); this interpreter supplies the slot's registers.
    result = evalPointwiseColorOp(opType, color, SlotMat[slot], SlotVec0[slot], SlotVec1[slot],
                                  SlotVec2[slot], SlotPacked[slot].w);
  } else if (opType == OP_BLEND) {
    // Blend with a constant operand held in SlotVec1; the selector's bit 8 picks the operand order
    // (the slot kernel's meaning of that bit; the chain's Blend reads register operands instead).
    // Select the arguments before the call so the compiler inlines xpBlendColors once instead of
    // twice (one per ternary branch).
    bool constFirst = ((SlotPacked[slot].w >> 8) & 1) != 0;
    vec4 blendSrc = constFirst ? SlotVec1[slot] : color;
    vec4 blendDst = constFirst ? color : SlotVec1[slot];
    int blendMode = SlotPacked[slot].w & 0xFF;
#if HAS_XP >= 1
    if (xpPass) {
      blendSrc = color;
      blendDst = xpDst;
      blendMode = XPBlendMode;
    }
#endif
    result = xpBlendColors(blendSrc, blendDst, blendMode);
  } else {  // OP_COLOR_SPACE_XFORM
    vec4 csColor = color;
    PM_APPLY_COLOR_SPACE_XFORM(csColor, ChainCSFlags, ChainSrcTF0, ChainSrcTF1, ChainSrcTFType,
                               ChainSrcOOTF, ChainColorXform, ChainDstOOTF, ChainDstTF0,
                               ChainDstTF1, ChainDstTFType)
    result = csColor;
  }
  return result;
}
