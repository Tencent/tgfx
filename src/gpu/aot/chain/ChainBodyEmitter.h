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
 * The runtime adapter over the contract-layer emitter (gpu/shaders/contract/ChainComposition.h):
 * it describes the selection's compiled program as a ChainComposition — the decoded structure the
 * contract layer consumes — and delegates the spelling to the shared core, so the runtime and the
 * build tool pre-composition cannot produce different bodies for the same shape.
 * @param program          The chain program the selection compiled for this draw.
 * @param xpNeedsBlendPass Whether the draw's xfer runs the loop's extra advanced-blend pass; the
 *                         body then spells that pass after the slot evaluations.
 * @return                 The replacement statements, or an empty string when a slot uses an
 *                         operator the emitter does not cover or reads a register no earlier slot
 *                         wrote; the caller then keeps the interpreter's loop.
 */
std::string EmitComposedChainBodyGLSL(const AOTProgram& program, bool xpNeedsBlendPass);

/**
 * Splices an emitted body into a chain fragment text, replacing everything between the executor
 * pragmas (both included).
 * @param program          The chain program the selection compiled for this draw.
 * @param xpNeedsBlendPass Whether the draw's xfer runs the extra advanced-blend pass.
 * @param code             The fragment source to rewrite in place.
 * @return                 False when the pragmas are absent (a text the current build did not
 *                         produce) or the emitter declined the program; the text is untouched.
 */
bool ComposeChainFragmentGLSL(const AOTProgram& program, bool xpNeedsBlendPass, std::string* code);

}  // namespace tgfx
