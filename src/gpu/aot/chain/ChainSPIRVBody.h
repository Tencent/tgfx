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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/aot/chain/ChainProgramPlan.h"
#include "gpu/shaders/Layout.h"

namespace tgfx {

/**
 * Emits the Vulkan linking route's body module: the runtime half the precompiled frame calls.
 * The body imports the plan's operators, declares the plan's own fragment uniform block (the
 * only block declaration in the whole program), threads the registers through the call
 * sequence, writes the epilogue and returns the final color. No entry point: the frame's
 * entry calls the exported chainBody. The module targets SPIR-V 1.0, the version the bundle's
 * op modules carry (the linker rejects a mismatch).
 * @param plan    The planned program (calls, uniform table, epilogue registers).
 * @param layout  The variant's derived layout (the chainBody parameter list follows it).
 * @param out     Receives the module words.
 * @param error   Receives the failure reason.
 */
bool EmitChainBodySPIRV(const ChainProgramPlan& plan, const Layout& layout,
                        std::vector<uint32_t>* out, std::string* error);

/**
 * Links the frame, the body and the op modules into one module in the Vulkan 1.0 environment
 * with id verification on. Returns false when an import is unresolved or the ids do not verify —
 * the caller treats that as "the assembled path cannot serve this draw" and keeps its fallback.
 * @param modules  The input modules; the frame (with the entry point) must be among them.
 * @param out      Receives the linked module.
 * @param error    Receives the linker's reason.
 */
bool LinkChainSPIRV(const std::vector<std::vector<uint32_t>>& modules, std::vector<uint32_t>* out,
                    std::string* error);

/**
 * The route's coverage gate: true when every operator the composition uses has a linkable module
 * and the variant carries no feature the body does not spell yet (the xfer legs, the device mask
 * and the texture leaves still run through the interpreter).
 */
bool ChainSPIRVBodySupported(const ChainComposition& composition, const Layout& layout, int hasXp);

}  // namespace tgfx
