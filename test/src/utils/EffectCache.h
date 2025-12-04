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

#pragma once

#include <list>
#include <memory>
#include <unordered_map>
#include "tgfx/gpu/GPU.h"

namespace tgfx {
struct EffectProgram {
  std::shared_ptr<RenderPipeline> pipeline = nullptr;
  uint32_t effectType = 0;
  std::list<EffectProgram*>::iterator cachedPosition;
};

class EffectCache {
 public:
  std::shared_ptr<RenderPipeline> findPipeline(uint32_t effectType);

  void addPipeline(uint32_t effectType, std::shared_ptr<RenderPipeline> pipeline);

 private:
  std::list<EffectProgram*> programLRU = {};
  std::unordered_map<uint32_t, std::unique_ptr<EffectProgram>> programMap = {};
};
}  // namespace tgfx
