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

#include "gpu/aot/AOTProgramInfo.h"
#include <string>
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledProgramCreator.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/Program.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/kernels/PointwiseChainShader.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
namespace {

// Distinguishes this route's keys from the plain route's processor-tree keys: the two routes key
// equivalent draws differently (this one never hashes a fragment-processor list), so a marker
// keeps them from ever colliding in the shared program cache.
constexpr uint32_t AOTRouteKeyTag = 0xA07D4E01u;

// A stable in-process hash of a shader name, so the key carries the family identity as one word.
uint32_t ShaderNameHash(const std::string& name) {
  uint32_t hash = 2166136261u;
  for (char character : name) {
    hash = (hash ^ static_cast<uint8_t>(character)) * 16777619u;
  }
  return hash;
}

}  // namespace

AOTProgramInfo::AOTProgramInfo(const ProgramInfo& info, const AOTProgram& program) {
  auto context = info.getContext();
  auto cache = context != nullptr ? context->precompiledShaderCache() : nullptr;
  if (cache == nullptr || !cache->isLoaded()) {
    // Without a bundle there is no route; the plain fallback records the miss itself.
    return;
  }
  auto variant = PointwiseChainShader::MatchProgram(program, &info);
  if (!variant) {
    return;
  }
  auto shaderInfo = ShaderRegistry::Find(PointwiseChainShader::Name);
  if (shaderInfo == nullptr || !IsBuildablePermutation(*shaderInfo, variant->vertPermutationIndex,
                                                       variant->fragPermutationIndex)) {
    return;
  }
  // From here on the route proceeds like a plain attempt whose match already ran, so the stage
  // funnel stays comparable with the plain route's.
  cache->recordAOTStage(PrecompiledAOTStage::Attempt);
  cache->recordAOTStage(PrecompiledAOTStage::CacheAvailable);
  cache->recordAOTStage(PrecompiledAOTStage::PermutationMatched);

  BytesKey key = {};
  key.write(AOTRouteKeyTag);
  key.write(ShaderNameHash(PointwiseChainShader::Name));
  key.write(variant->vertPermutationIndex);
  key.write(variant->fragPermutationIndex);
  auto gp = info.getGeometryProcessor();
  if (gp != nullptr) {
    gp->computeProcessorKey(context, &key);
  }
  info.getXferProcessor()->computeProcessorKey(context, &key);
  info.writePipelineStateKey(&key);

  auto globalCache = context->globalCache();
  auto resolved = globalCache->findProgram(key);
  if (resolved == nullptr) {
    PermutationMatchResult match = {PointwiseChainShader::Name, variant->vertPermutationIndex,
                                    variant->fragPermutationIndex};
    resolved = PrecompiledProgramCreator::CreateProgramFromMatch(context, &info, match);
    if (resolved == nullptr) {
      return;
    }
    globalCache->addProgram(key, resolved, cache->deliberateMissMarking());
  }
  resolvedProgram = resolved;
}

}  // namespace tgfx
