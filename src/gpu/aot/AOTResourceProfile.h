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
#include <string>
#include <vector>
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/modules/Chain.h"

namespace tgfx {

// A compiled chain program's capacity class: how large a pool it needs and which of the extended
// sources (noise, LUT gradient, tiled recipes) it carries. This is not the executor's PROFILE —
// that one describes which fields the draw's execution needs and is derived per draw
// (FamilyDomains' ChainProfileOfCell) — so the two are distinct vocabularies on purpose:
//   Chain      -> a plain profile (0..2): the 16-slot pool
//   NoiseGraph -> the noise profile (6): the 32-slot pool plus one noise source
//   TiledGraph -> a gradient profile (3..5): the pool plus one LUT gradient and three recipes
// Automatic means "not decided yet": the planner picks the smallest class that fits.
enum class AOTResourceProfile : uint32_t {
  Automatic,
  Chain,
  NoiseGraph,
  TiledGraph,
};

// What a chain pass asks for. Only the fields below differ between the shapes; the limits every
// shape shares (color-space transform, clip rect and rrect blocks, result registers) are enforced
// by AOTChainBuilder::ValidateSlots.
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
inline constexpr size_t MaxChainLeafBindings = ChainProfileLeafBindings;

// The three shapes are points of the declaration's per-PROFILE table (shaders/modules/Chain.h):
// each number below is that profile's declared array length, so the capacity a shape offers and
// the variant that will be assembled from the profile cannot disagree. The gradient shape shares
// the plain chain's 16-slot storage: the libpag corpus's heaviest gradient chain needs 7 slots
// (p100 over 2,468 constructed chains), so 16 keeps ample headroom.
inline constexpr AOTResourceCapacity ChainResourceCapacity = {
    ChainSlotPackedCounts[0], ChainProfileTiledRecipes[0], ChainProfileNoiseSources[0],
    ChainProfileGradientLeaves[0]};
inline constexpr AOTResourceCapacity NoiseGraphResourceCapacity = {
    ChainSlotPackedCounts[6], ChainProfileTiledRecipes[6], ChainProfileNoiseSources[6],
    ChainProfileGradientLeaves[6]};
inline constexpr AOTResourceCapacity TiledGraphResourceCapacity = {
    ChainSlotPackedCounts[3], ChainProfileTiledRecipes[3], ChainProfileNoiseSources[3],
    ChainProfileGradientLeaves[3]};

// The capacity defines the compiled stages receive live in the tool (core/CapacityDefines.h):
// they are read from the declaration the assembler sizes the arrays from, and the contract header
// below cannot include the declaration layer. The planner's budgets stay here; Chain.h
// pins them to the declared counts with static_asserts.

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

// The three shapes are exactly three declared profile points: a shape's capacity must equal the
// profile it selects, or the planner's budget and the assembled variant disagree.
static_assert(ChainResourceCapacity.instructions == ChainSlotPackedCounts[0] &&
                  ChainResourceCapacity.tiledRecipes == ChainProfileTiledRecipes[0],
              "the plain shape must be the plain profile's declared capacity");
static_assert(NoiseGraphResourceCapacity.instructions == ChainSlotPackedCounts[6] &&
                  NoiseGraphResourceCapacity.noiseSources == ChainProfileNoiseSources[6],
              "the noise shape must be the noise profile's declared capacity");
static_assert(TiledGraphResourceCapacity.gradients == ChainProfileGradientLeaves[3] &&
                  TiledGraphResourceCapacity.tiledRecipes == ChainProfileTiledRecipes[3],
              "the gradient shape must be a gradient profile's declared capacity");

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
