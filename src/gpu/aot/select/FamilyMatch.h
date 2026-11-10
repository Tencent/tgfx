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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "gpu/shaders/Family.h"

namespace tgfx {

struct DrawShape;
class ProgramInfo;
struct AOTTextureParameters;
struct AOTGradientParameters;
struct AOTOpaqueKernelParameters;

/// One declaration-driven family's match: the variant the draw maps to and the module data
/// sources the writers read (the tiled leaf for the TiledTexture slot, the mask leaf for the
/// MaskTexture slot and the color graph's gradient node for the Gradient slot, null when absent).
struct FamilyMatchResult {
  Variant variant;
  const AOTTextureParameters* tiledLeaf = nullptr;
  const AOTTextureParameters* maskLeaf = nullptr;
  const AOTGradientParameters* gradient = nullptr;
  const AOTOpaqueKernelParameters* blurKernel = nullptr;
  const AOTTextureParameters* blurChild = nullptr;
  const AOTOpaqueKernelParameters* glassKernel = nullptr;
};

/// One declared family's match rule: the shape predicate plus the variant encoding, writing the
/// matched variant and its data-source leaves into the result. The info is the draw's program
/// being matched — the interpreter family compiles its carried IR through it (exactly what the
/// legacy chain's Accept did), every other rule ignores it.
using FamilyMatchFunction = bool (*)(const ProgramInfo* info, const DrawShape& shape,
                                     FamilyMatchResult* result);

/// The match rules, one per family table row, addressed by the family index (FamilyCount()
/// entries; a row without a rule is a build failure). MatchAssembledFamily walks them for a
/// draw's family; the declared writer calls its program's own rule alone to collect the
/// per-draw data sources.
const FamilyMatchFunction* FamilyMatchers();

/// Matches a draw against the declaration-driven family table. The rules are pairwise disjoint
/// (a debug-build tripwire walks them all and reports any second acceptor as a
/// [family-conflict] failure), so in release the first acceptor — whichever it is — is the only
/// one and the walk order carries no priority. Returns nullopt when no assembled family serves
/// the shape; the caller then falls back to the legacy kernel table (the transitional route
/// until P3 removes it).
std::optional<FamilyMatchResult> MatchAssembledFamily(const ProgramInfo* info,
                                                      const DrawShape& shape);

/// The declared Chain family's traffic switch (default off): while the legacy interpreter still
/// serves chain draws, the declared rule must not claim them. Flipping it on lets the declared
/// family win the walk; the legacy chain then becomes unreachable and 2c deletes it.
void SetDeclaredChainEnabled(bool enabled);
bool DeclaredChainEnabled();

}  // namespace tgfx
