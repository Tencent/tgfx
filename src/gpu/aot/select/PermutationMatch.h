/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the License is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include "gpu/shaders/contract/AOTFailure.h"

namespace tgfx {

class ProgramInfo;

/**
 * The matched precompiled family of one program shape, as the permutation matcher resolves it
 * (the variant-only form the family Match functions return is PermutationVariant in
 * ShaderPermutation.h).
 */
struct PermutationMatchResult {
  std::string shaderName;
  uint32_t vertPermutationIndex;
  uint32_t fragPermutationIndex;
};

// The vocabulary itself lives in gpu/shaders/contract/AOTFailure.h: the matcher's rejection reasons and the
// cache's fallback categories are one enum, so a new reason is appended once. The sub-reasons
// between GlGateRejected and PermutationNotEnumerated still report as NoMatchingRule to the
// coarse fallback category; they exist so a miss says where it was rejected.

/**
 * One match outcome with its failure reason: the reason is meaningful only when match is nullopt,
 * but caching them together keeps "matched", "missed" and "never run" distinguishable with a
 * single optional on ProgramInfo.
 */
struct PermutationMatchOutcome {
  std::optional<PermutationMatchResult> match;
  PermutationMatchFailure failure = PermutationMatchFailure::None;
};

/// Stable name of a failure value for diagnostics dumps. Never null.
const char* PermutationMatchFailureName(PermutationMatchFailure failure);

/**
 * Matches the program info against the precompiled families (the full family walk plus the
 * post-match validations) and caches the outcome on the info: the first caller runs the walk, and
 * every later lookup of the same info — the program creator's, a prepare-time probe's — reuses
 * it. The cache makes the lookup idempotent, so callers may probe freely.
 */
std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo,
                                                       PermutationMatchFailure* failure = nullptr);

}  // namespace tgfx
