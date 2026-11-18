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

#include <cstdint>
#include <string>
#include "gpu/shaders/Family.h"
#include "gpu/shaders/contract/ChainComposition.h"
#include "gpu/shaders/contract/ChainPlan.h"

namespace tgfx {

class PrecompiledShaderCache;

/**
 * Assembles a complete fragment shader the way the old JIT's ProgramBuilder did: a single walk
 * generates every declaration exactly once (the uniform block, the varyings and the samplers from
 * the family's module declarations — the same walk the build's assembler and the runtime's layout
 * derivation read), and the code is a set of per-op pack entries — the pure fragments, the
 * generated loaders, the xfer support resolved by fact, the resource evaluators — each included at
 * most once. Nothing is extracted from a stored variant text, so no definition can collide and no
 * unused library can ride along; what the composition does not use does not exist.
 * @param composition  The shape to assemble.
 * @param family       The matched family (the module walk's input).
 * @param variant      The matched variant (the walk's dim values).
 * @param cache        The loaded bundle's pack.
 * @param code         Receives the assembled text.
 * @return             False when the shape is outside the slice or a pack entry is missing; the
 *                     caller keeps its other paths.
 */
/// Assembles the vertex stage from the stored variant text and the plan: the attributes, the
/// vertex block and the geometry hook stay byte-identical (the writer's addresses), while the
/// varying set and the Chain hook's coordinate writes follow the plan's contract.
bool AssembleChainVertexGLSL(const ChainPlan& plan, const std::string& storedVs, std::string* code);

bool AssembleChainFragmentGLSL(const ChainPlan& plan, const ChainComposition& composition,
                               const Family& family, const Variant& variant,
                               const PrecompiledShaderCache* cache, std::string* code);

}  // namespace tgfx
