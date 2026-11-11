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
#include "gpu/shaders/ShaderPermutation.h"
#include "gpu/shaders/contract/AOTFailure.h"

namespace tgfx {

class ProgramInfo;

/**
 * The matched precompiled family of one program shape, as the permutation matcher resolves it:
 * the family name plus the variant its Compose encoded, so every stage of the program-creation
 * pipeline names the two indices through PermutationVariant instead of re-declaring them.
 */
struct PermutationMatchResult {
  std::string shaderName;
  PermutationVariant variant = {};
};

// The vocabulary itself lives in gpu/shaders/contract/AOTFailure.h: the matcher's rejection reasons and the
// cache's fallback categories are one enum, so a new reason is appended once. The sub-reasons
// between GlGateRejected and PermutationNotEnumerated still report as NoMatchingRule to the
// coarse fallback category; they exist so a miss says where it was rejected.

/**
 * Matches the program info against the precompiled families through the one selection entry
 * (SelectProgram, see ProgramSelection.h) with the draw's own expression in scope. Kept as a
 * thin wrapper for the callers that only need the family and variant; the program creator and
 * the pass route call SelectProgram directly.
 */
std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo,
                                                       PermutationMatchFailure* failure = nullptr);

}  // namespace tgfx
