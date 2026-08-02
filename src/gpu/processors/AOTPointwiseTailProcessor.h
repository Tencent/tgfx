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
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <vector>
#include "gpu/AOTEffect.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

enum class AOTPointwiseOpType : int {
  ColorMatrix = 0,
  Luma = 1,
  AlphaThreshold = 2,
  ColorSpaceXform = 3,
  None = 4,
};

struct AOTPointwiseSlot {
  AOTPointwiseOpType type = AOTPointwiseOpType::None;
  AOTColorMatrixParameters colorMatrix = {};
  AOTLumaParameters luma = {};
  AOTAlphaThresholdParameters alphaThreshold = {};
  AOTColorSpaceXformParameters colorSpaceXform = {};
};

class AOTPointwiseTailProcessor : public FragmentProcessor {
 public:
  enum class SourceKind : int {
    Plain = 0,
    Device = 1,
  };

  static constexpr size_t MaxSlots = 2;

  static PlacementPtr<AOTPointwiseTailProcessor> Make(BlockAllocator* allocator,
                                                      PlacementPtr<FragmentProcessor> source,
                                                      const std::vector<AOTPointwiseSlot>& slots);

  AOTPointwiseTailProcessor(PlacementPtr<FragmentProcessor> source, SourceKind sourceKind,
                            const std::vector<AOTPointwiseSlot>& slots);

  std::string name() const override {
    return "AOTPointwiseTailProcessor";
  }

  SourceKind sourceKind() const {
    return _sourceKind;
  }

  size_t slotCount() const {
    return _slotCount;
  }

  const AOTPointwiseSlot& slot(size_t index) const {
    return slots[index];
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

  SourceKind _sourceKind = SourceKind::Plain;
  size_t _slotCount = 0;
  std::array<AOTPointwiseSlot, MaxSlots> slots = {};
  CoordTransform deviceCoordTransform = {};
};

}  // namespace tgfx
