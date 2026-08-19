// Shared pointwise-operator logic. Provides applyPointwiseOp(vec4), which applies exactly one
// premultiplied-in/premultiplied-out pointwise operator selected at runtime by the OpType uniform:
//   0 = ColorMatrix, 1 = Luma, 2 = AlphaThreshold, 3 = ColorSpaceXform, 4 = None (passthrough).
// OP_NONE lets a producer kernel (e.g. PerlinNoiseFillShader) emit its raw premultiplied output when
// no operator FP is composed on top of it, while the same OpType uniform absorbs the composed case.
// Requires the pointwise_op_uniforms.inc fields to be present in the FragmentUniformBlock.
//
// The field names below are plain for single-slot kernels. A multi-slot kernel defines each name
// to its array-element form (e.g. #define OpType OpType[slot]) before including, so one function
// body serves every slot; the slot parameter is unused in the scalar form. The operator/flag
// constants are guarded so repeated inclusion is a no-op rather than a redefinition.
#ifndef TGFX_POINTWISE_OP_CONSTANTS
#define TGFX_POINTWISE_OP_CONSTANTS
#define OP_COLOR_MATRIX 0
#define OP_LUMA 1
#define OP_ALPHA_THRESHOLD 2
#define OP_COLOR_SPACE_XFORM 3
#define OP_NONE 4
// Values 6/7 mirror the chain kernel's slot ABI (5 is OP_TEXTURE there, never a pointwise op).
#define OP_CONST_COLOR 6
#define OP_BLEND 7

#define CS_UNPREMUL 1
#define CS_LINEARIZE 2
#define CS_GAMUT 4
#define CS_ENCODE 8
#define CS_PREMUL 16
#define CS_SRC_OOTF 32
#define CS_DST_OOTF 64
#endif

// OP_BLEND needs the shared PorterDuff blend math regardless of the kernel's XP state; the
// include is guarded, so the per-slot re-inclusions of this file stay harmless.
#include "transfer_function.inc"
#include "xp_blend_colors.inc"

// ColorSpaceXform transfer functions bound to this slot's uniform names.
float src_tf(int slot, float x) {
  return transferTF(x, SrcTF0, SrcTF1, SrcTFType);
}

float dst_tf(int slot, float x) {
  return transferTF(x, DstTF0, DstTF1, DstTFType);
}

// Applies the selected pointwise operator to a premultiplied color and returns a premultiplied color.
vec4 applyPointwiseOp(int slot, vec4 color) {
  if (OpType == OP_NONE) {
    return color;
  }
  vec4 result;
  if (OpType == OP_COLOR_MATRIX) {
    // Unpremultiply, apply matrix + vector, clamp, re-premultiply.
    color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
    color = clamp(ColorMatrix * color + ColorVector, 0.0, 1.0);
    color.rgb *= color.a;
    result = color;
  } else if (OpType == OP_LUMA) {
    result = vec4(dot(color.rgb, vec3(Kr, Kg, Kb)));
  } else if (OpType == OP_ALPHA_THRESHOLD) {
    result = vec4(0.0);
    if (color.a > 0.0) {
      result.rgb = color.rgb / color.a;
      result.a = step(Threshold, color.a);
      result = clamp(result, 0.0, 1.0);
    }
  } else if (OpType == OP_CONST_COLOR) {
    // Matches the chain kernel's const-color op and the runtime ConstColorProcessor emission:
    // 0 = Ignore (replace), 1 = ModulateRGBA, 2 = ModulateA.
    if (ConstInputMode == 1) {
      result = ConstColorValue * color;
    } else if (ConstInputMode == 2) {
      result = ConstColorValue * color.a;
    } else {
      result = ConstColorValue;
    }
  } else if (OpType == OP_BLEND) {
    // Blend with a constant operand; the operand order follows the source fragment processor's
    // child position (BlendConstFirst mirrors XfermodeFragmentProcessor::Child). Select the
    // arguments before the call so the compiler inlines xpBlendColors once instead of twice
    // (one per ternary branch).
    vec4 blendSrc = BlendConstFirst != 0 ? ConstColorValue : color;
    vec4 blendDst = BlendConstFirst != 0 ? color : ConstColorValue;
    result = xpBlendColors(blendSrc, blendDst, BlendModeValue);
  } else {  // OP_COLOR_SPACE_XFORM
    if ((CSFlags & CS_UNPREMUL) != 0) {
      float a = color.a;
      color = (a > 0.0) ? vec4(color.rgb / a, a) : vec4(0.0);
    }
    if ((CSFlags & CS_LINEARIZE) != 0) {
      color.r = src_tf(slot, color.r);
      color.g = src_tf(slot, color.g);
      color.b = src_tf(slot, color.b);
    }
    if ((CSFlags & CS_SRC_OOTF) != 0) {
      float srcY = dot(color.rgb, SrcOOTF.rgb);
      color.rgb *= sign(srcY) * pow(abs(srcY), SrcOOTF.a);
    }
    if ((CSFlags & CS_GAMUT) != 0) {
      color.rgb = ColorXform * color.rgb;
    }
    if ((CSFlags & CS_DST_OOTF) != 0) {
      float dstY = dot(color.rgb, DstOOTF.rgb);
      color.rgb *= sign(dstY) * pow(abs(dstY), DstOOTF.a);
    }
    if ((CSFlags & CS_ENCODE) != 0) {
      color.r = dst_tf(slot, color.r);
      color.g = dst_tf(slot, color.g);
      color.b = dst_tf(slot, color.b);
    }
    if ((CSFlags & CS_PREMUL) != 0) {
      color.rgb *= color.a;
    }
    result = color;
  }
  return result;
}
