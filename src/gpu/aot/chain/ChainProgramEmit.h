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
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include "gpu/aot/chain/ChainProgramPlan.h"

namespace tgfx {

class PrecompiledShaderCache;

/**
 * Prints one planned chain program as GLSL: the declarations from the plan's tables, the source
 * functions its flags call for, the pack entries its operator set reaches, the straight-line body
 * from the call list, and the composed tail. The text is a pure function of the plan, so the
 * assembled path stays byte-identical to the pre-split emission.
 * @param plan   The plan to print.
 * @param cache  The loaded bundle's pack.
 * @param code   Receives the fragment text.
 * @return       False when a pack entry the plan needs is missing.
 */
bool PrintChainProgramGLSL(const ChainProgramPlan& plan, const PrecompiledShaderCache* cache,
                           std::string* code, bool esProfile, int blendMode);

/**
 * Emits a complete chain fragment the way the JIT's ProgramBuilder did: the neutral walk plans the
 * shape (one pass registers every uniform, varying and sampler it reads), and the GLSL printer
 * spells it. A member exists only because the emission that reads it registered it, so the
 * declaration is the emission's closure: no parameter pool, no packed slot array, no
 * capacity-sized arrays, no prefix the writer has to know about.
 * @param composition  The shape to emit.
 * @param family       The matched family (the variant walk's input).
 * @param variant      The matched variant (its geometry, texture count and xfer fact).
 * @param cache        The loaded bundle's pack.
 * @param code         Receives the fragment text.
 * @param bindings     Receives the declaration tables the upload walks.
 * @return             False when the shape is outside the slice or a pack entry is missing; the
 *                     caller keeps its other paths.
 */
bool EmitChainProgramGLSL(const ChainComposition& composition, const Family& family,
                          const Variant& variant, const PrecompiledShaderCache* cache,
                          std::string* code, ChainBindings* bindings, bool esProfile,
                          int blendMode);

/**
 * Assembles the vertex stage from the stored variant text and the emit's varying contract: the
 * attributes, the vertex block and the geometry hook stay byte-identical (the writer's addresses),
 * while the varying set follows the composition.
 * @param bindings   The emit's contract: the leaf coordinate varyings and the resource reads.
 * @param storedVs   The variant's stored vertex text.
 * @param code       Receives the vertex text.
 * @return           False when the stored text carries no chain vertex hook.
 */
bool AssembleChainVertexGLSL(const ChainBindings& bindings, const std::string& storedVs,
                             std::string* code);

}  // namespace tgfx
