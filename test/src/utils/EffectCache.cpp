/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "EffectCache.h"

namespace tgfx {
static constexpr size_t MAX_PROGRAM_COUNT = 128;

std::shared_ptr<RenderPipeline> EffectCache::findPipeline(uint32_t effectType) {
  auto result = programMap.find(effectType);
  if (result != programMap.end()) {
    auto& program = result->second;
    programLRU.erase(program->cachedPosition);
    programLRU.push_front(program.get());
    program->cachedPosition = programLRU.begin();
    return program->pipeline;
  }
  return nullptr;
}

void EffectCache::addPipeline(uint32_t effectType, std::shared_ptr<RenderPipeline> pipeline) {
  if (pipeline == nullptr) {
    return;
  }
  auto program = std::make_unique<EffectProgram>();
  program->effectType = effectType;
  program->pipeline = std::move(pipeline);
  programLRU.push_front(program.get());
  program->cachedPosition = programLRU.begin();
  programMap[effectType] = std::move(program);
  while (programLRU.size() > MAX_PROGRAM_COUNT) {
    auto oldProgram = programLRU.back();
    programLRU.pop_back();
    programMap.erase(oldProgram->effectType);
  }
}

}  // namespace tgfx
