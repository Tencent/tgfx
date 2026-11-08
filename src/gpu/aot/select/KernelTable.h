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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/aot/select/PermutationMatch.h"
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

/** The table's answer: which family accepted, and the variant it composed. */
struct AOTKernelMatch {
  const char* family = nullptr;
  PermutationVariant variant = {};
};

/**
 * The first dedicated family whose predicate accepts the facts, or nullopt when none does. The
 * rows are the generated family list (TGFX_FOR_EACH_SHADER_FAMILY) — the same list the bundle
 * enumeration and the tests derive from, so a family joins the dispatch by existing, with no
 * hand-maintained registry to forget. FamilyExclusivityTest proves the acceptances pairwise
 * disjoint, so the iteration order (file-name order, the interpreter last) is not semantics:
 * the first acceptor is the only acceptor.
 */
std::optional<AOTKernelMatch> KernelTableLookup(const DrawShape& facts,
                                                PermutationMatchFailure* failure);

}  // namespace tgfx
