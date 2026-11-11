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
#include "gpu/aot/select/PrecompiledProgramCreator.h"
#include "gpu/aot/select/ProgramKey.h"
#include "gpu/aot/select/ProgramSelection.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

AOTProgramInfo::AOTProgramInfo(const ProgramInfo& info) {
  auto context = info.getContext();
  auto cache = context != nullptr ? ContextCacheAccess::Cache(context) : nullptr;
  if (cache == nullptr || !cache->isLoaded()) {
    // Without a bundle there is no route; the plain fallback records the miss itself.
    return;
  }
  // The pass scope: the expression is already compiled, so only the interpreter can serve it.
  // SelectProgram runs the same admission, buildability, and specialization steps the plain
  // lookup applies, so a draw these gates refuse falls back to the original draw instead of
  // rendering wrong pixels.
  auto selection = SelectProgram(context, info, SelectionScope::ChainOnly, nullptr);
  if (!selection) {
    return;
  }
  // From here on the route proceeds like a plain attempt whose match already ran, so the stage
  // funnel stays comparable with the plain route's.
  cache->recordAOTStage(PrecompiledAOTStage::Attempt);
  cache->recordAOTStage(PrecompiledAOTStage::CacheAvailable);
  cache->recordAOTStage(PrecompiledAOTStage::PermutationMatched);

  // The specialization values join the key: two effect trees of one variant (same GP, same XP,
  // same slot-count class) encode different chains, so the second shape must not silently reuse
  // the first shape's program. SelectProgram resolved them through the same SpecResolver entry
  // the plain route uses, so both routes agree.
  const BytesKey key = MakeProgramKey(context, info, selection->variant.id(), selection->spec);

  auto globalCache = context->globalCache();
  auto resolved = globalCache->findProgram(key);
  if (resolved == nullptr) {
    resolved = PrecompiledProgramCreator::CreateProgramForVariant(
        context, &info, selection->variant, selection->spec.empty() ? nullptr : &selection->spec);
    if (resolved == nullptr) {
      return;
    }
    globalCache->addProgram(key, resolved);
  }
  resolvedProgram = resolved;
}

}  // namespace tgfx
