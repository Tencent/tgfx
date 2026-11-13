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
#include "gpu/aot/select/FamilyMatchResult.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/shaders/Family.h"

namespace tgfx {

struct DrawShape;
class ProgramInfo;

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
/// (a debug-build tripwire walks the rows after the first acceptor and reports any second one as
/// a [family-conflict] failure), so the first acceptor is the only one and the walk order carries
/// no priority. Returns nullopt when no curated rule accepts the shape; the caller then consults
/// the fallback rows (the Chain interpreter) and, failing those, drops the draw.
std::optional<FamilyMatchResult> MatchAssembledFamily(const ProgramInfo* info,
                                                      const DrawShape& shape);

/// The declared Chain's own match, for the decomposition route: the interpreter is not a shape
/// rule but the carried IR's compilability, so it is consulted behind the decomposition policy
/// rather than inside the dedicated-kernel walk (a policy can disable that walk while still
/// asking the interpreter to serve the draw).
std::optional<FamilyMatchResult> MatchChainProgram(const ProgramInfo* info);

/// The variant a compiled chain program resolves to: the draw's geometry facts plus the program's
/// expression shape, or nullopt when the shape has no declared counterpart. The caller already
/// holds the program, so this is the entry for routes and tests that run before any cache lookup;
/// MatchChainProgram is the program-resolving wrapper.
std::optional<Variant> MatchChainVariant(const AOTProgram& program, const ProgramInfo* info);

}  // namespace tgfx
