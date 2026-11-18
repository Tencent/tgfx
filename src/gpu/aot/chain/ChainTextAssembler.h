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
#include "gpu/shaders/contract/ChainComposition.h"

namespace tgfx {

class PrecompiledShaderCache;

/**
 * Whether the composition's operators and structural facts are all inside the assembled slice's
 * vocabulary: the pointwise color operators, Blend, the inline expressions, the analytic coverage
 * operators and plain (non-tiled) texture leaves. The geometry check (the rect interface's
 * geometry coverage prelude) and the xfer's advanced-blend pass stay with the caller, which knows
 * the matched variant.
 */
bool ChainCompositionAssembles(const ChainComposition& composition);

/**
 * Assembles a complete fragment shader from the bundle's pack: the matched variant's interface
 * chunk (its own declaration prefix and designator sources, extracted by the build), the
 * generated loaders, the used fragments' texts and their dependencies, the leaf prefetches, the
 * straight-line body and the tail. The uniform block is the interpreter's own declaration, so the
 * writer and the upload path are unchanged; what disappears is everything the composition does
 * not use — the operator dispatch library, the spec constants, the gates and the executor loop.
 * @param composition  The shape to assemble (must pass ChainCompositionAssembles).
 * @param variantId    The matched variant whose interface chunk the pack carries.
 * @param cache        The loaded bundle's pack.
 * @param code         Receives the assembled text.
 * @return             False when a pack entry is missing (the caller keeps the other paths).
 */
bool AssembleChainFragmentGLSL(const ChainComposition& composition, uint32_t variantId,
                               const PrecompiledShaderCache* cache, std::string* code);

}  // namespace tgfx
