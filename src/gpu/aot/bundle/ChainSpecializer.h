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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include "gpu/aot/ir/Program.h"

namespace tgfx {

/**
 * Demo-stage runtime specialization of the pointwise chain interpreter (TGFX_AOT_SPECIALIZE=1).
 *
 * The chain artifact's GLSL carries two pragma-marked regions (see
 * pointwise_fragment_main.inc): the structural accessors and the slot executor. The generic
 * bodies read the uniform block, so an unspecialized compile is behaviorally identical to today.
 * This pass rewrites both regions with one draw's folded constants — the packed slot words, the
 * slot count and the three epilogue registers — so the driver's compiler eliminates every op
 * branch, dispatch compare and dynamic register index the draw does not use, without relying on
 * driver loop unrolling (the executor region is replaced with one call site per slot).
 *
 * GLSL-text backends only for now; the SPIR-V based backends will inject the same values through
 * their native specialization mechanisms (Vulkan specialization constants, Metal function
 * constants, WebGPU overrides).
 */
bool ChainSpecializationEnabled();

/**
 * Returns the specialized GLSL text for one chain fragment artifact, or an empty string when the
 * text does not carry the specialization regions (or anything else looks unexpected) — the
 * caller then keeps the generic artifact and nothing changes.
 */
std::string SpecializeChainFragmentGLSL(const std::string& artifactText, const AOTProgram& program);

}  // namespace tgfx
