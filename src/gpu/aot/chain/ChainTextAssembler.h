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
#include "gpu/shaders/contract/ChainComposition.h"

namespace tgfx {

class PrecompiledShaderCache;

/**
 * Whether the composition's operators are all inside the rect-entry slice's vocabulary: the pure
 * pointwise color operators, Blend, the inline expressions and the analytic coverage operators.
 * Texture, gradient and noise operators still take the interpreter route.
 */
bool ChainCompositionAssemblesRect(const ChainComposition& composition);

/**
 * Assembles a complete fragment shader from the bundle's pack: the rect interface chunk, the
 * generated loaders, the used fragments' texts and their dependencies, the straight-line body and
 * the tail. The uniform block is the interpreter's own declaration, so the writer and the upload
 * path are unchanged; what disappears is everything the composition does not use — the operator
 * dispatch library, the spec constants, the gates and the executor loop.
 * @param composition  The shape to assemble (must pass ChainCompositionAssemblesRect).
 * @param cache        The loaded bundle's pack.
 * @param code         Receives the assembled text.
 * @return             False when a pack entry is missing (the caller keeps the other paths).
 */
bool AssembleChainFragmentRectGLSL(const ChainComposition& composition,
                                   const PrecompiledShaderCache* cache, std::string* code);

}  // namespace tgfx
