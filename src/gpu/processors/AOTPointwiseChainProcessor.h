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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <vector>
#include "gpu/AOTEffect.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

/// The runtime operator of one chain slot. The integer values are part of the precompiled kernel
/// ABI: they must match the OP_* constants in pointwise_chain.inc.
enum class AOTChainOp : int {
  ColorMatrix = 0,
  Luma = 1,
  AlphaThreshold = 2,
  ColorSpaceXform = 3,
  None = 4,
  Texture = 5,
  ConstColor = 6,
  Blend = 7,
};

/// One node of a pointwise DAG after flattening into the fused kernel's slot array. in0/in1 are
/// slot indices of this node's inputs: -1 selects the geometry color (the Color uniform), -2 marks
/// an unused input. Only Blend uses both inputs.
struct AOTChainSlot {
  AOTChainOp op = AOTChainOp::None;
  int in0 = -2;
  int in1 = -2;
  AOTColorMatrixParameters colorMatrix = {};
  AOTLumaParameters luma = {};
  AOTAlphaThresholdParameters alphaThreshold = {};
  AOTColorSpaceXformParameters colorSpaceXform = {};
  AOTConstColorParameters constColor = {};
  AOTBlendParameters blend = {};
};

/**
 * A FragmentProcessor that evaluates an entire pointwise DAG in one pass through the precompiled
 * PointwiseChainShader. Texture leaves are registered as child processors in slot order: child k
 * pairs with slot k and samples TextureSampler_k, which keeps sampler indexing static in the
 * kernel. Everything else about the DAG — op types, wiring, blend modes, operator parameters —
 * travels in uniforms, so one program variant serves any topology with the same leaf count.
 */
class AOTPointwiseChainProcessor : public FragmentProcessor {
 public:
  static constexpr size_t MaxSlots = 16;

  static PlacementPtr<AOTPointwiseChainProcessor> Make(
      BlockAllocator* allocator, std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
      const std::vector<AOTChainSlot>& slots, size_t rootSlot);

  AOTPointwiseChainProcessor(std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
                             const std::vector<AOTChainSlot>& slots, size_t rootSlot);

  std::string name() const override {
    return "AOTPointwiseChainProcessor";
  }

  size_t leafCount() const {
    return numChildProcessors();
  }

  size_t slotCount() const {
    return _slotCount;
  }

  size_t root() const {
    return rootSlot;
  }

  const AOTChainSlot& slot(size_t index) const {
    return slots[index];
  }

  void emitCode(EmitArgs& args) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

 private:
  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onSetData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const override;

  size_t _slotCount = 0;
  size_t rootSlot = 0;
  std::array<AOTChainSlot, MaxSlots> slots = {};
};

}  // namespace tgfx
