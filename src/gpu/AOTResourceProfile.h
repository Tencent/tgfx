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

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace tgfx {

enum class AOTResourceProfile : uint32_t {
  Automatic,
  Chain,
  NoiseGraph,
  TiledGraph,
};

struct AOTResourceDemand {
  size_t instructions = 0;
  size_t registers = 0;
  size_t leafBindings = 0;
  size_t tiledRecipes = 0;
  size_t noiseSources = 0;
  size_t gradients = 0;
  size_t colorSpaceXforms = 0;
  size_t deviceRects = 0;
  size_t localRects = 0;
  size_t rrects = 0;
};

struct AOTResourceCapacity {
  size_t instructions = 16;
  size_t registers = 16;
  size_t leafBindings = 4;
  size_t tiledRecipes = 2;
  size_t noiseSources = 0;
  size_t gradients = 0;
  size_t colorSpaceXforms = 1;
  size_t deviceRects = 2;
  size_t localRects = 2;
  size_t rrects = 4;
};

inline constexpr AOTResourceCapacity ChainResourceCapacity = {};
inline constexpr AOTResourceCapacity NoiseGraphResourceCapacity = {32, 16, 4, 2, 1, 0, 1, 2, 2, 4};
inline constexpr AOTResourceCapacity TiledGraphResourceCapacity = {32, 16, 4, 3, 0, 1, 1, 2, 2, 4};

inline const AOTResourceCapacity* GetAOTResourceCapacity(AOTResourceProfile profile) {
  switch (profile) {
    case AOTResourceProfile::Chain:
      return &ChainResourceCapacity;
    case AOTResourceProfile::NoiseGraph:
      return &NoiseGraphResourceCapacity;
    case AOTResourceProfile::TiledGraph:
      return &TiledGraphResourceCapacity;
    default:
      return nullptr;
  }
}

inline bool IsAOTGraphProfile(AOTResourceProfile profile) {
  return profile == AOTResourceProfile::NoiseGraph || profile == AOTResourceProfile::TiledGraph;
}

inline bool FitsAOTResourceProfile(const AOTResourceDemand& demand, AOTResourceProfile profile) {
  const auto* capacity = GetAOTResourceCapacity(profile);
  if (capacity == nullptr ||
      (profile == AOTResourceProfile::NoiseGraph) != (demand.noiseSources > 0)) {
    return false;
  }
  return demand.instructions <= capacity->instructions && demand.registers <= capacity->registers &&
         demand.leafBindings <= capacity->leafBindings &&
         demand.tiledRecipes <= capacity->tiledRecipes &&
         demand.noiseSources <= capacity->noiseSources && demand.gradients <= capacity->gradients &&
         demand.colorSpaceXforms <= capacity->colorSpaceXforms &&
         demand.deviceRects <= capacity->deviceRects && demand.localRects <= capacity->localRects &&
         demand.rrects <= capacity->rrects;
}

inline AOTResourceProfile SelectAOTResourceProfile(const AOTResourceDemand& demand) {
  for (auto profile : {AOTResourceProfile::Chain, AOTResourceProfile::NoiseGraph,
                       AOTResourceProfile::TiledGraph}) {
    if (FitsAOTResourceProfile(demand, profile)) {
      return profile;
    }
  }
  return AOTResourceProfile::Automatic;
}

}  // namespace tgfx
