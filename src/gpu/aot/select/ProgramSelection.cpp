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

#include "gpu/aot/select/ProgramSelection.h"
#include "gpu/ContextInternal.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/aot/select/ShapeKey.h"
#include "gpu/aot/spec/SpecResolver.h"
#include "gpu/shaders/Families.h"

namespace tgfx {

std::optional<ProgramSelection> SelectProgram(Context* context, const ProgramInfo& info,
                                              AOTFailure* failure, ShapeKey* shapeKey,
                                              bool* answeredFromCache) {
  // The route-independent gates first, so a draw they refuse reports its reason before any family
  // walk runs; the texture-read check needs the matched family and runs after the walk.
  const auto shape = ExtractDrawShape(&info);
  // The attempt key is the draw's structure, not its values: a shape refused once stays refused
  // however its colours and matrices change, and a shape served once is not blocked by another
  // draw's values.
  const ShapeKey key = MakeShapeKey(shape, info);
  if (shapeKey != nullptr) {
    *shapeKey = key;
  }
  auto* negativeCache = context != nullptr ? ContextCacheAccess::Cache(context) : nullptr;
  if (negativeCache != nullptr && negativeCache->hasProgramMisses()) {
    AOTFailure cachedReason = AOTFailure::Unspecified;
    if (negativeCache->findProgramMiss(key, &cachedReason)) {
      // The table already holds this shape's verdict; counting it again as a fresh miss would
      // double the metrics, so the caller is told the answer came from the cache.
      negativeCache->recordCachedMiss(cachedReason);
      if (answeredFromCache != nullptr) {
        *answeredFromCache = true;
      }
      return std::nullopt;
    }
  }
  if (!AdmitMatchedProgram(&info, shape, nullptr, failure)) {
    return std::nullopt;
  }
  auto matchFailure = AOTFailure::None;
  std::optional<FamilyMatchResult> match = std::nullopt;
  const auto* carried = info.getCarriedIR();
  if (carried != nullptr && carried->carriedChainProgram() != nullptr) {
    // The expression is already compiled: a plan pass built it, or the draw's own earlier match
    // did. The interpreter is then the only family that can serve it — walking the rules would
    // re-derive a variant the compiled program has already fixed.
    match = MatchChainProgram(&info);
  } else {
    // Alpha-only (AAAA) targets: only the kernels that apply the write swizzle at output may
    // serve the draw; the row predicates carry that rejection.
    const bool alphaOnly = info.getOutputSwizzle() == Swizzle::AAAA();
    auto* shaderCache = context != nullptr ? ContextCacheAccess::Cache(context) : nullptr;
    const auto policy =
        shaderCache != nullptr ? shaderCache->aotPolicy() : PrecompiledShaderCache::AOTPolicy{};
    // Gradients keep the interpreter even with the decomposition switch off (only the extended
    // chain kind serves them), mirroring the required-gradient-lowering the switch retained.
    const bool allowInterpreter = policy.decomposition || shape.hasGradientSource;
    if (policy.dedicatedKernels) {
      // The declared walk already consults the interpreter's fallback row (MatchChain is its
      // matcher), so one walk answers both; a second explicit interpreter attempt would run the
      // same predicate on the same shape.
      match = MatchAssembledFamily(&info, shape);
    } else if (allowInterpreter) {
      // The chain serves alpha-only targets too: its kernels apply the render-target write swizzle
      // themselves, so no target-type gate applies here.
      match = MatchChainProgram(&info);
    }
    if (!match && alphaOnly) {
      matchFailure = AOTFailure::NoMatchingFamily;
    }
  }
  if (!match) {
    // No rejecting site named a reason: the generic no-matching-rule verdict.
    if (matchFailure == AOTFailure::None) {
      matchFailure = AOTFailure::NoMatchingRule;
    }
    if (failure != nullptr) {
      *failure = matchFailure;
    }
    return std::nullopt;
  }
  // The same acceptance every route shares, now with the matched family's declared texture-read
  // semantics in play. The variant came out of EncodeVariant, so it is inside the family's
  // reachable set by construction — a buildable-permutation re-check here would be a tautology
  // over the same declaration.
  if (!AdmitMatchedProgram(&info, shape, &Families()[match->variant.family], failure)) {
    return std::nullopt;
  }
  // The specialization values are part of the program's identity: the processor tree cannot tell
  // two chain shapes apart (a rect clip's anti-alias flag, for one, is not in it), so a program
  // built for one shape must never answer the other.
  // The blend switch's fact needs both the slot array and the draw's xfer: an advanced shader-side
  // mode runs the loop's blend pass even when no slot carries a Blend op.
  const bool needsBlendMath =
      match->chain != nullptr &&
      ChainNeedsBlendMath(*match->chain,
                          XpNeedsAdvancedMath(info.getXferFacts().shaderBlend(),
                                              static_cast<int>(info.getBlendMode())));
  const SpecValues spec =
      SpecResolver::Resolve(context != nullptr ? ContextCacheAccess::Cache(context) : nullptr,
                            info.backend(), match->variant, match->chain, needsBlendMath);
  if (failure != nullptr) {
    *failure = AOTFailure::None;
  }
  ProgramSelection selection = {};
  selection.variant = match->variant;
  selection.spec = spec;
  selection.sources = *match;
  return selection;
}

}  // namespace tgfx
