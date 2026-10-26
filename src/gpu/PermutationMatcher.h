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

/// The dispatch loop's output: the matched family plus its resolved variant indices (the
/// variant-only form the family Match functions return is PermutationVariant in
/// ShaderPermutation.h).
struct PermutationMatchResult {
  std::string shaderName;
  uint32_t vertPermutationIndex;
  uint32_t fragPermutationIndex;
};

enum class PermutationMatchFailure {
  None,
  UnsupportedOutputSwizzle,
  NoMatchingRule,
  /// A rule matched, but the draw samples a texture whose read semantics (e.g. the GRAY_8 / RG_88
  /// read swizzle) the matched shader family does not declare in PrecompiledShaderInfo.
  UnsupportedTextureRead,
  // Sub-reasons below all report as NoMatchingRule to the coarse fallback category; they exist so a
  // miss says where it was rejected instead of only "no rule matched". Append only.
  /// The OpenGL entry gate refused the pipeline (bundle profile mismatch or a non-2D sampler).
  GlGateRejected,
  /// The chain path refuses quads that carry a vertex subset attribute.
  QuadSubset,
  /// More than one gradient (or gradient + noise) in a single chain pass.
  GradientOrNoiseBudget,
  /// The draw carries a coverage FP shape the candidate kernel cannot evaluate.
  CoverageShape,
  /// No family matched the geometry processor at all.
  NoMatchingFamily,
  /// The pipeline composed, but that combination is not in the enumerated variant set.
  PermutationNotEnumerated,
  /// Building the AOT graph failed because a processor has no lowering (the default
  /// FragmentProcessor::lowerToAOT).
  MissingLowering,
};

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
                                                       PermutationMatchFailure* failure = nullptr);

}  // namespace tgfx
