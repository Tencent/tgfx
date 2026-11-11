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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "gpu/aot/spec/SpecValues.h"
#include "gpu/shaders/Family.h"
#include "gpu/shaders/contract/AOTFailure.h"

namespace tgfx {

class Context;
class ProgramInfo;

/**
 * One draw's resolved program: the variant that serves it and the specialization values folded
 * into the shader. The per-draw data sources the writer reads are deliberately absent — the
 * writer still re-matches for them; carrying them here is the next step's change.
 */
struct ProgramSelection {
  Variant variant = {};
  SpecValues spec = {};
};

/**
 * Which expressions a lookup may consider:
 *   AnyFamily  a draw's own expression — the curated rules, then the interpreter's fallback row
 *   ChainOnly  a plan pass — the expression is already compiled, only the interpreter carries it
 */
enum class SelectionScope {
  AnyFamily,
  ChainOnly,
};

/**
 * The one selection entry both routes call: the route-independent admission gates, the family
 * walk (or the interpreter alone, for a plan pass), the buildability and per-family admission
 * checks, and the specialization capture. failure is written only when the result is null; a
 * plain no-match defaults to NoMatchingRule unless a gate named a finer reason, and a success
 * reports None.
 */
std::optional<ProgramSelection> SelectProgram(Context* context, const ProgramInfo& info,
                                              SelectionScope scope,
                                              PermutationMatchFailure* failure = nullptr);

}  // namespace tgfx
