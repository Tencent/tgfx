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
#include "gpu/aot/ir/Program.h"

namespace tgfx {

/**
 * Emits the GLSL statements that replace the chain executor loop (the TGFX_COMPOSED_EXECUTOR
 * markers in pointwise_fragment_main.inc): one straight-line evaluation of the program's slots
 * where every instruction becomes a local vec4 whose inputs are earlier locals, the geometry
 * color or the coverage unit. The opcode dispatch, the selector reads and the result-register
 * array disappear from the compiled text, while the values still upload through the same data
 * pool — the emitted loads read it with literal offsets and the per-operator calls are the same
 * functions the interpreter dispatches to.
 * @param program          The chain program the selection compiled for this draw.
 * @param xpNeedsBlendPass Whether the draw's xfer runs the loop's extra advanced-blend pass; the
 *                         body then spells that pass after the slot evaluations.
 * @return                 The replacement statements, or an empty string when a slot uses an
 *                         operator this emitter does not cover or reads a register no earlier slot
 *                         wrote; the caller then keeps the interpreter's loop.
 */
std::string EmitComposedChainBodyGLSL(const AOTProgram& program, bool xpNeedsBlendPass);

/**
 * Splices an emitted body into a chain fragment text, replacing everything between the executor
 * markers (both included).
 * @param program          The chain program the selection compiled for this draw.
 * @param xpNeedsBlendPass Whether the draw's xfer runs the extra advanced-blend pass.
 * @param code             The fragment source to rewrite in place.
 * @return                 False when the markers are absent (a text the current build did not
 *                         produce) or the emitter declined the program; the text is untouched.
 */
bool ComposeChainFragmentGLSL(const AOTProgram& program, bool xpNeedsBlendPass, std::string* code);

}  // namespace tgfx
