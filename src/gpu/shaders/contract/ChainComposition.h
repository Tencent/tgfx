/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>

namespace tgfx {

/**
 * One instruction of a composed chain body, the way the runtime's decoded program expresses it
 * and the emitter consumes it: the operator's ABI value (ChainISA's constants), the two input
 * designators (-1 geometry color, -3 coverage unit, -4 white, -5 opaque geometry color, any other
 * negative zero, >=0 a result register), the result register, the precomputed selector word (the
 * ChainSlotSelector encoding; Texture and TexModulate read it), and the Blend and ConstColor small
 * values the call sites marshal. Values and texture identities are absent by construction — a
 * composition names code shape only.
 */
struct ChainCompositionSlot {
  int op = 0;
  int in0 = -2;
  int in1 = -2;
  int outRegister = -1;
  int selector = 0;
  int blendMode = 0;
  bool multiplyInputAlpha = false;
  int inputMode = 0;
};

/**
 * The structure a composed chain body evaluates: the slots in instruction order, the three
 * epilogue registers the fragment's tail consumes, and whether the xfer's advanced blend pass runs
 * after the slots. This is the layer-neutral contract between whoever knows a draw's shape (the
 * runtime's AOTProgram, or the build tool pre-composing a known shape) and the GLSL emitter, so
 * both spell one body from one description.
 */
struct ChainComposition {
  std::vector<ChainCompositionSlot> slots = {};
  int rootRegister = 0;
  int coverageRootRegister = -1;
  int clipCoverageRegister = -1;
  bool xpNeedsBlendPass = false;
};

/**
 * Emits the GLSL statements that replace the chain executor loop (the tgfx_composed_executor
 * pragmas in pointwise_fragment_main.inc): one straight-line evaluation of the composition's
 * slots where every instruction becomes a local vec4 whose inputs are earlier locals, the fixed
 * designator source functions or literals. The opcode dispatch, the selector reads and the
 * result-register array disappear from the compiled text, while the values still upload through
 * the same data pool — the emitted loads read it with literal offsets and the per-operator calls
 * are the same functions the interpreter dispatches to.
 * @param composition  The shape to spell.
 * @return             The replacement statements, or an empty string when a slot uses an operator
 *                     this emitter does not cover or reads a register no earlier slot wrote; the
 *                     caller then keeps the interpreter's loop.
 */
std::string EmitComposedChainBodyGLSL(const ChainComposition& composition);

/**
 * Splices an emitted body into a chain fragment text, replacing everything between the executor
 * pragmas (both included).
 * @param composition  The shape to spell.
 * @param code         The fragment source to rewrite in place.
 * @return             False when the pragmas are absent (a text the current build did not
 *                     produce) or the emitter declined the composition; the text is untouched.
 */
bool ComposeChainFragmentGLSL(const ChainComposition& composition, std::string* code);

}  // namespace tgfx
