// The shared pointwise color-operator dispatch: one branch per operator of the op list
// (TGFX_POINTWISE_OPS in KernelContract.h), reading the operator's block from the chain data
// pool (the layout lives in contract/ChainOps.h; the readers are generated from it). The chain
// interpreter (pointwise_chain_eval.inc) and the slot interpreter (pointwise_op.inc, used by
// the slot kernels and the YUV tail) both call evalPointwiseColorOp, so the operator-to-parameter
// mapping is written once — a caller only supplies its slot's pool offset.
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

// This file reads the specialization surface (the gate below), so it declares itself a reader.
#pragma tgfx_spec_reader

#include "operator/pointwise_math.inc"

// Applies exactly one pointwise color operator to a premultiplied color and returns a
// premultiplied color. `op` is the operator's ABI value, `at` the slot's data-pool offset, and
// `mode` the caller's selector word (the low byte carries the operator's small value). An
// operator this function does not dispatch (the exemptions, or a non-color-op value) returns the
// color unchanged.
//
// Every chain variant carries this dispatch, but most draws use none of it: a draw whose slots
// hold no color operator never reaches a call site (the specialized `Packed.x` folds each branch
// away), so the runtime splices the body out and the driver parses one stub instead.
#pragma tgfx_spec_gate ColorOps : (SPEC_UsedOps() & ((1 << OP_COLOR_MATRIX) | (1 << OP_LUMA) | (1 << OP_ALPHA_THRESHOLD) | (1 << OP_CONST_COLOR))) != 0
vec4 evalPointwiseColorOp(int op, vec4 color, int at, int mode) {
  if (op == OP_COLOR_MATRIX) {
    ColorMatrixData d = chainLoadColorMatrix(at);
    return pmApplyColorMatrix(color, d.linear, d.translate);
  }
  if (op == OP_LUMA) {
    LumaData d = chainLoadLuma(at);
    return pmApplyLuma(color, vec3(d.kr, d.kg, d.kb));
  }
  if (op == OP_ALPHA_THRESHOLD) {
    AlphaThresholdData d = chainLoadAlphaThreshold(at);
    return pmApplyAlphaThreshold(color, d.threshold);
  }
  if (op == OP_CONST_COLOR) {
    ConstColorData d = chainLoadConstColor(at);
    return pmApplyConstColor(d.color, color, mode & 0xF);
  }
  return color;
}

#endif
