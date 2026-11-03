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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

namespace tgfx {

/**
 * Why a precompiled program did not serve a draw, in one vocabulary. Two layers used to define
 * their own enums — the matcher's per-family rejection reasons and the cache's coarse fallback
 * categories — and the second was derived from the first by a mapping function. Both names remain
 * as aliases of this type, so call sites read naturally while new reasons are appended in one
 * place.
 *
 * The match-side values (from UnsupportedOutputSwizzle through PermutationNotEnumerated) describe
 * where the walk rejected the draw; the cache-side values (from CacheNotLoaded through
 * UnsupportedTextureRead) describe what happened after a match, or why there was no match to make.
 * Append only: the name tables and the reported statistics index by these values.
 */
enum class AOTFailure : uint8_t {
  None = 0,
  // ---- matcher-side rejections ----
  /// The target's output swizzle is one the precompiled kernels do not implement.
  UnsupportedOutputSwizzle,
  /// No family accepted the draw.
  NoMatchingRule,
  /// A family matched, but the draw samples a texture whose read semantics (e.g. the GRAY_8 /
  /// RG_88 read swizzle) the family does not declare in PrecompiledShaderInfo.
  UnsupportedTextureRead,
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
  // ---- cache-side fallbacks ----
  /// No bundle was loaded when the draw was prepared.
  CacheNotLoaded,
  /// The pipeline carries a texture that is not instantiated yet (a lazy/deferred proxy): the
  /// runtime drops such content via a stub program by design, so this is not a coverage gap.
  DeferredTexture,
  VertexArtifactMissing,
  FragmentArtifactMissing,
  VertexModuleCreationFailed,
  FragmentModuleCreationFailed,
  PipelineCreationFailed,
  Unspecified,
  /// No bundle is embedded for this backend, so there is nothing to look an artifact up in: the
  /// backend has no precompiled shader support (D3D12), or this build embeds none for it.
  NoBundleForBackend,
  /// A bundle is embedded but the cache refused to load it (toolchain ABI, profile tag, identity
  /// hash, malformed layout).
  BundleRejected,
  /// The variant's reflected vertex inputs do not line up with the runtime geometry processor's
  /// attribute list.
  VertexLayoutMismatch,
  /// One past the last reason, for fixed-size tables.
  Count,
};

/// The matcher's historical name for the same vocabulary.
using PermutationMatchFailure = AOTFailure;

/// The cache's historical name for the same vocabulary.
using PrecompiledFallbackReason = AOTFailure;

/// The one name table. The diagnostics print these, and the coverage gate counts by them, so a
/// new reason must be named here in the same commit that appends it.
inline const char* AOTFailureName(AOTFailure failure) {
  switch (failure) {
    case AOTFailure::None:
      return "None";
    case AOTFailure::UnsupportedOutputSwizzle:
      return "UnsupportedOutputSwizzle";
    case AOTFailure::NoMatchingRule:
      return "NoMatchingRule";
    case AOTFailure::UnsupportedTextureRead:
      return "UnsupportedTextureRead";
    case AOTFailure::GlGateRejected:
      return "GlGateRejected";
    case AOTFailure::QuadSubset:
      return "QuadSubset";
    case AOTFailure::GradientOrNoiseBudget:
      return "GradientOrNoiseBudget";
    case AOTFailure::CoverageShape:
      return "CoverageShape";
    case AOTFailure::NoMatchingFamily:
      return "NoMatchingFamily";
    case AOTFailure::PermutationNotEnumerated:
      return "PermutationNotEnumerated";
    case AOTFailure::CacheNotLoaded:
      return "CacheNotLoaded";
    case AOTFailure::DeferredTexture:
      return "DeferredTexture";
    case AOTFailure::VertexArtifactMissing:
      return "VertexArtifactMissing";
    case AOTFailure::FragmentArtifactMissing:
      return "FragmentArtifactMissing";
    case AOTFailure::VertexModuleCreationFailed:
      return "VertexModuleCreationFailed";
    case AOTFailure::FragmentModuleCreationFailed:
      return "FragmentModuleCreationFailed";
    case AOTFailure::PipelineCreationFailed:
      return "PipelineCreationFailed";
    case AOTFailure::Unspecified:
      return "Unspecified";
    case AOTFailure::NoBundleForBackend:
      return "NoBundleForBackend";
    case AOTFailure::BundleRejected:
      return "BundleRejected";
    case AOTFailure::VertexLayoutMismatch:
      return "VertexLayoutMismatch";
    case AOTFailure::Count:
      return "Count";
  }
  return "Unknown";
}

}  // namespace tgfx
