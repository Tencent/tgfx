// The shared pointwise color-operator dispatch: one branch per operator of the op list
// (TGFX_POINTWISE_OPS in KernelContract.h), reading the chain slot ABI's parameter registers
// (the allocation table in pipeline/chain/pointwise_chain_uniforms.inc). The chain interpreter
// (pointwise_chain_eval.inc) and the slot interpreter (pointwise_op.inc, used by TexturedEffect
// and the YUV tail) both call evalPointwiseColorOp, so "which register holds operator X's
// parameters" is written once — a caller only supplies where its register values come from
// (chain slot arrays, or a kernel's scalar uniform fields).
//
// Register conventions (fixed by the allocation table):
//   m  = SlotMat    v0 = SlotVec0    v1 = SlotVec1    v2 = SlotVec2
//   ColorMatrix      m (the 4x4 part) + v0 (the translate vector)
//   Luma             v2.rgb
//   AlphaThreshold   v2.a
//   ConstColor       v1; `mode` carries the input mode in its low byte
//
// Registered exceptions, dispatched by the call sites because their operands are not per-slot
// parameter registers:
//   tgfx_pointwise_exempt Blend: the operands differ per interpreter (chain registers plus the xp
//     pass, or a kernel's const operand with its order bit); the blend math itself is shared
//     through xpBlendColors.
//   tgfx_pointwise_exempt ColorSpaceXform: reads the program-level transfer-function block
//     (PM_APPLY_COLOR_SPACE_XFORM), whose field names differ per kernel layout.
//
// shader_build_tool checks this file against the op list (every operator needs a branch or an
// exemption here, and no branch may name an operator the list no longer holds).
#ifndef TGFX_POINTWISE_COLOR_DISPATCH_INC
#define TGFX_POINTWISE_COLOR_DISPATCH_INC

#include "operator/pointwise_math.inc"

// Applies exactly one pointwise color operator to a premultiplied color and returns a
// premultiplied color. `op` is the operator's ABI value; `mode` is the caller's selector word
// (the low byte carries the operator's small value). An operator this function does not dispatch
// (the exemptions, or a non-color-op value) returns the color unchanged.
vec4 evalPointwiseColorOp(int op, vec4 color, mat4 m, vec4 v0, vec4 v1, vec4 v2, int mode) {
  if (op == OP_COLOR_MATRIX) {
    return pmApplyColorMatrix(color, m, v0);
  }
  if (op == OP_LUMA) {
    return pmApplyLuma(color, v2.rgb);
  }
  if (op == OP_ALPHA_THRESHOLD) {
    return pmApplyAlphaThreshold(color, v2.a);
  }
  if (op == OP_CONST_COLOR) {
    return pmApplyConstColor(v1, color, mode & 0xF);
  }
  return color;
}

#endif
