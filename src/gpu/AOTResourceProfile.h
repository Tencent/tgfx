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

namespace tgfx {

// The pointwise kernel comes in three resource shapes, one per shader variant family:
//   Chain      -> PointwiseChainShader
//   NoiseGraph -> PointwiseGraphShader, RESOURCE_KIND 0 (one noise source)
//   TiledGraph -> PointwiseGraphShader, RESOURCE_KIND 1 (one gradient, three tiled recipes)
// Automatic means "not decided yet": the planner picks the smallest shape that fits.
enum class AOTResourceProfile : uint32_t {
  Automatic,
  Chain,
  NoiseGraph,
  TiledGraph,
};

// What a chain pass asks for. Only the fields below differ between the shapes; the limits every
// shape shares (color-space transform, clip rect and rrect blocks, result registers) are enforced
// by AOTPointwiseChainProcessor::ValidateSlots.
struct AOTResourceDemand {
  size_t instructions = 0;
  size_t leafBindings = 0;  // texture leaves + the LUT gradient + two noise tables.
  size_t tiledRecipes = 0;
  size_t noiseSources = 0;
  size_t gradients = 0;
};

// What a shape offers; each number is the declared length of the matching GLSL array or module.
struct AOTResourceCapacity {
  size_t instructions;  // CHAIN_SLOT_CAPACITY in the shader.
  size_t tiledRecipes;
  size_t noiseSources;
  size_t gradients;
};

// The sampler bindings a kernel artifact carries before padding.
inline constexpr size_t MaxChainLeafBindings = 4;

inline constexpr AOTResourceCapacity ChainResourceCapacity = {16, 2, 0, 0};
inline constexpr AOTResourceCapacity NoiseGraphResourceCapacity = {32, 2, 1, 0};
inline constexpr AOTResourceCapacity TiledGraphResourceCapacity = {32, 3, 0, 1};

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
  // NoiseGraph is the only shape that carries the noise module, and it must be used for it.
  if (capacity == nullptr ||
      (profile == AOTResourceProfile::NoiseGraph) != (demand.noiseSources > 0)) {
    return false;
  }
  return demand.instructions <= capacity->instructions &&
         demand.leafBindings <= MaxChainLeafBindings &&
         demand.tiledRecipes <= capacity->tiledRecipes &&
         demand.noiseSources <= capacity->noiseSources && demand.gradients <= capacity->gradients;
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
