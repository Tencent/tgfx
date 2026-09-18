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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <vector>
#include "gpu/AOTEffect.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

/**
 * A FragmentProcessor for a YUV video source with a bounded color-grade tail: the multi-plane
 * TextureEffect source plus up to three pointwise-operator slots, mapping onto one
 * YUVTextureFillShader program (plane sampling and conversion followed by the per-slot operator
 * loop). Built only by the AOT chain builder from a DecomposeYUVChain plan; the plain runtime
 * route keeps composing the source with ordinary operator FPs.
 */
class AOTYUVChainProcessor : public FragmentProcessor {
 public:
  // The precompiled YUVTextureFillShader carries three pointwise-operator slots (the same
  // capacity PerlinNoiseFillShader uses), covering the real color-grade shapes (composed color
  // matrices collapse into one). Longer tails are rejected at planning time.
  static constexpr size_t MaxPointwiseSlots = 3;

  static PlacementPtr<AOTYUVChainProcessor> Make(BlockAllocator* allocator,
                                                 PlacementPtr<FragmentProcessor> source,
                                                 const std::vector<AOTPointwiseSlot>& slots);

  AOTYUVChainProcessor(PlacementPtr<FragmentProcessor> source,
                       const std::vector<AOTPointwiseSlot>& slots);

  std::string name() const override {
    return "AOTYUVChainProcessor";
  }

  size_t pointwiseSlotCount() const {
    return slotCount;
  }

  const AOTPointwiseSlot& pointwiseSlot(size_t index) const {
    return pointwiseSlots[index];
  }

  const FragmentProcessor* source() const {
    return childProcessor(0);
  }

  void emitCode(EmitArgs& args) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

 private:
  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onSetData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const override;

  size_t slotCount = 0;
  std::array<AOTPointwiseSlot, MaxPointwiseSlots> pointwiseSlots = {};
};

}  // namespace tgfx
