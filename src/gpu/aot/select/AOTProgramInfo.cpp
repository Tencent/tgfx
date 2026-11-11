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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/AOTProgramInfo.h"
#include <string>
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/ir/Program.h"
#include "gpu/aot/select/PrecompiledProgramCreator.h"
#include "gpu/aot/select/ProgramKey.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/aot/spec/SpecResolver.h"
#include "gpu/shaders/ChainInputs.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

AOTProgramInfo::AOTProgramInfo(const ProgramInfo& info, const AOTProgram& program) {
  auto context = info.getContext();
  auto cache = context != nullptr ? ContextCacheAccess::Cache(context) : nullptr;
  if (cache == nullptr || !cache->isLoaded()) {
    // Without a bundle there is no route; the plain fallback records the miss itself.
    return;
  }
  auto variant = ChainInputs::MatchVariant(program, &info);
  if (!variant) {
    return;
  }
  auto shaderInfo = ShaderRegistry::Find("Chain");
  if (shaderInfo == nullptr ||
      !IsBuildablePermutation(*shaderInfo, variant->ordinal, variant->ordinal)) {
    return;
  }
  // The same acceptance the plain lookup applies: a draw these gates refuse must not slip through
  // the pass-IR route, or it would render with wrong pixels instead of falling back to the
  // original draw.
  const auto shape = ExtractDrawShape(&info);
  if (!AdmitMatchedProgram(&info, shape, shaderInfo, nullptr)) {
    return;
  }
  // From here on the route proceeds like a plain attempt whose match already ran, so the stage
  // funnel stays comparable with the plain route's.
  cache->recordAOTStage(PrecompiledAOTStage::Attempt);
  cache->recordAOTStage(PrecompiledAOTStage::CacheAvailable);
  cache->recordAOTStage(PrecompiledAOTStage::PermutationMatched);

  // The variant's identity does not name the chain's shape: two effect trees of one variant
  // (same GP, same XP, same slot-count class) encode different chains, so the specialization
  // values join the key or the second shape would silently reuse the first shape's program. The
  // entry is the same one the plain route uses (SpecResolver), so both routes agree.
  const SpecValues spec =
      SpecResolver::Resolve(ContextCacheAccess::Cache(context), context->backend(), info, "Chain",
                            variant->ordinal, variant->ordinal);
  const BytesKey key = MakeProgramKey(context, info, variant->id(), spec);

  auto globalCache = context->globalCache();
  auto resolved = globalCache->findProgram(key);
  if (resolved == nullptr) {
    PermutationMatchResult match = {"Chain", {variant->ordinal, variant->ordinal}};
    resolved = PrecompiledProgramCreator::CreateProgramFromMatch(context, &info, match,
                                                                 spec.empty() ? nullptr : &spec);
    if (resolved == nullptr) {
      return;
    }
    globalCache->addProgram(key, resolved);
  }
  resolvedProgram = resolved;
}

}  // namespace tgfx
