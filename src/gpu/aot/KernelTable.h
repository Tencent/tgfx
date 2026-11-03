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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <vector>
#include "gpu/PermutationMatch.h"
#include "gpu/aot/DrawShape.h"
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

/**
 * One executor-table row: a dedicated family's acceptance as a function of the draw's extracted
 * facts. The shape predicates read the IR's plain data (the family's ExtractFacts — the same
 * body its Extract delegates to), and the compose half stays the family's own, so a row and the
 * walk it migrated from share one implementation. Rows are tried in table order — the walk
 * order they migrate from, which is ledger semantics.
 */
struct AOTKernelRow {
  const char* family = nullptr;
  std::optional<PermutationVariant> (*match)(const DrawShape&, PermutationMatchFailure*) = nullptr;
  // The family's ServesAlphaOnly declaration, hoisted out of the family bodies: the lookup skips
  // a non-serving row on an alpha-only target, so the gate is stated once instead of 21 times.
  bool servesAlphaOnly = false;
};

/** The table's answer: which row accepted first, and the variant it composed. */
struct AOTKernelMatch {
  const AOTKernelRow* row = nullptr;
  PermutationVariant variant = {};
};

/** The first row whose predicate accepts the facts, or nullopt when none does. */
std::optional<AOTKernelMatch> KernelTableLookup(const DrawShape& facts,
                                                PermutationMatchFailure* failure);

}  // namespace tgfx
