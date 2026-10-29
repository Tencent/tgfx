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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <string>
#include "gpu/ProgramInfo.h"

namespace tgfx {

/// Stable name of a failure value for diagnostics dumps. Never null.
const char* PermutationMatchFailureName(PermutationMatchFailure failure);

/**
 * Attempts to match a ProgramInfo's processor combination against known precompiled shader
 * patterns. Returns the shader name and permutation index if matched, or nullopt if no precompiled
 * variant covers this combination. When failure is not null, it receives None on success, an
 * applicable candidate's rejection reason on failure, or NoMatchingRule when no candidate records
 * a reason. Probes that do not apply to the pipeline must not record a rejection reason.
 */
std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo,
                                                       PermutationMatchFailure* failure);

}  // namespace tgfx
