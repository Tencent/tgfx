// Shared pointwise-operator logic. Provides applyPointwiseOp(vec4), which applies exactly one
// premultiplied-in/premultiplied-out pointwise operator selected at runtime by the OpType uniform:
//   0 = ColorMatrix, 1 = Luma, 2 = AlphaThreshold, 3 = ColorSpaceXform, 4 = None (passthrough).
// OP_NONE lets a producer kernel (e.g. PerlinNoiseFillShader) emit its raw premultiplied output when
// no operator FP is composed on top of it, while the same OpType uniform absorbs the composed case.
// Requires the pointwise_op_uniforms.inc fields to be present in the FragmentUniformBlock.
//
// The field names below are plain for single-slot kernels. A multi-slot kernel defines each name
// to its array-element form (e.g. #define OpType OpType[slot]) before including, so one function
// body serves every slot; the slot parameter is unused in the scalar form. The OP_* constants come
// from the shared op table; its guard makes the repeated per-slot inclusion of this file a no-op
// rather than a redefinition. This interpreter dispatches on the AOTPointwiseOpType subset; the
// chain-only defines in the table are simply unreferenced here.
#include "operator/op_codes.inc"

// OP_BLEND needs the shared PorterDuff blend math regardless of the kernel's XP state; the
// include is guarded, so the per-slot re-inclusions of this file stay harmless.
#include "xp/xp_blend_colors.inc"
#include "operator/pointwise_math.inc"

// Applies the selected pointwise operator to a premultiplied color and returns a premultiplied color.
#if HAS_XP >= 1
// With xpPass set, this is instead the XferProcessor's advanced blend pass (xp_blend_pass.inc):
// it blends `color`, the XferProcessor source, with xpDst in XPBlendMode at the OP_BLEND site, so
// the kernel inlines xpBlendColors once. The caller keeps `slot` in range on that pass.
vec4 applyPointwiseOp(int slot, vec4 color, bool xpPass, vec4 xpDst) {
  int opType = xpPass ? OP_BLEND : OpType;
#else
vec4 applyPointwiseOp(int slot, vec4 color) {
  int opType = OpType;
#endif
  if (opType == OP_NONE) {
    return color;
  }
  vec4 result;
  if (opType == OP_COLOR_MATRIX) {
    result = pmApplyColorMatrix(color, ColorMatrix, ColorVector);
  } else if (opType == OP_LUMA) {
    result = pmApplyLuma(color, vec3(Kr, Kg, Kb));
  } else if (opType == OP_ALPHA_THRESHOLD) {
    result = pmApplyAlphaThreshold(color, Threshold);
  } else if (opType == OP_CONST_COLOR) {
    // Matches the chain kernel's const-color op and the runtime ConstColorProcessor emission.
    result = pmApplyConstColor(ConstColorValue, color, ConstInputMode);
  } else if (opType == OP_BLEND) {
    // Blend with a constant operand; the operand order follows the source fragment processor's
    // child position (BlendConstFirst mirrors XfermodeFragmentProcessor::Child). Select the
    // arguments before the call so the compiler inlines xpBlendColors once instead of twice
    // (one per ternary branch).
    vec4 blendSrc = BlendConstFirst != 0 ? ConstColorValue : color;
    vec4 blendDst = BlendConstFirst != 0 ? color : ConstColorValue;
    int blendMode = BlendModeValue;
#if HAS_XP >= 1
    if (xpPass) {
      blendSrc = color;
      blendDst = xpDst;
      blendMode = XPBlendMode;
    }
#endif
    result = xpBlendColors(blendSrc, blendDst, blendMode);
  } else {  // OP_COLOR_SPACE_XFORM
    PM_APPLY_COLOR_SPACE_XFORM(color, CSFlags, SrcTF0, SrcTF1, SrcTFType, SrcOOTF, ColorXform,
                               DstOOTF, DstTF0, DstTF1, DstTFType)
    result = color;
  }
  return result;
}
