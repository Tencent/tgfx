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

#include "gpu/processors/AOTPointwiseTailProcessor.h"
#include "gpu/AOTPointwiseSlotWriter.h"

namespace tgfx {
PlacementPtr<AOTPointwiseTailProcessor> AOTPointwiseTailProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> source,
    const std::vector<AOTPointwiseSlot>& slots) {
  if (allocator == nullptr || source == nullptr || source->numTextureSamplers() != 1 ||
      slots.size() > MaxSlots) {
    return nullptr;
  }
  SourceKind sourceKind = SourceKind::Plain;
  if (source->name() == "DeviceSpaceTextureEffect") {
    sourceKind = SourceKind::Device;
  } else if (source->name() != "TextureEffect") {
    return nullptr;
  }
  for (const auto& slot : slots) {
    if (slot.type == AOTPointwiseOpType::None) {
      return nullptr;
    }
    if (slot.type == AOTPointwiseOpType::ColorSpaceXform &&
        !ColorSpaceXformStepsSupported(slot.colorSpaceXform.steps.get())) {
      return nullptr;
    }
  }
  return allocator->make<AOTPointwiseTailProcessor>(std::move(source), sourceKind, slots);
}

AOTPointwiseTailProcessor::AOTPointwiseTailProcessor(PlacementPtr<FragmentProcessor> source,
                                                     SourceKind sourceKind,
                                                     const std::vector<AOTPointwiseSlot>& slots)
    : FragmentProcessor(ClassID()), _sourceKind(sourceKind), _slotCount(slots.size()),
      deviceCoordTransform(Matrix::I()) {
  for (size_t index = 0; index < slots.size(); ++index) {
    this->slots[index] = slots[index];
  }
  if (_sourceKind == SourceKind::Device) {
    addCoordTransform(&deviceCoordTransform);
  }
  registerChildProcessor(std::move(source));
}

void AOTPointwiseTailProcessor::emitCode(EmitArgs& args) const {
  emitChild(0, args.inputColor, args);
  EmitAOTPointwiseSlots(args, slots.data(), _slotCount, MaxSlots, "pointwiseTail");
}

void AOTPointwiseTailProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(_sourceKind));
  bytesKey->write(static_cast<uint32_t>(_slotCount));
  for (size_t index = 0; index < _slotCount; ++index) {
    bytesKey->write(static_cast<uint32_t>(slots[index].type));
    if (slots[index].type == AOTPointwiseOpType::ColorSpaceXform) {
      // The emitted color-space code depends on which steps are enabled and on the transfer-function
      // types, so the key must separate programs that differ in those, exactly as
      // ColorSpaceXformEffect does.
      bytesKey->write(ColorSpaceXformSteps::XFormKey(slots[index].colorSpaceXform.steps.get()));
    }
  }
}

void AOTPointwiseTailProcessor::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  if (fragmentUniformData == nullptr) {
    return;
  }
  fragmentUniformData->setDataOptional("SourceKind", static_cast<int>(_sourceKind));
  // Runtime loop bound of the precompiled kernel; a uniform so the slot loop stays rolled and the
  // operator library exists once in the binary.
  fragmentUniformData->setDataOptional("PointwiseSlotCount", static_cast<int>(MaxSlots));
  for (size_t index = 0; index < MaxSlots; ++index) {
    UploadAOTPointwiseSlot(fragmentUniformData, index, MaxSlots, slots[index]);
  }
}

}  // namespace tgfx
