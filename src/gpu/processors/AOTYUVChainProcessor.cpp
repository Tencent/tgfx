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

#include "gpu/processors/AOTYUVChainProcessor.h"
#include "gpu/AOTPointwiseSlotWriter.h"

namespace tgfx {

PlacementPtr<AOTYUVChainProcessor> AOTYUVChainProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> source,
    const std::vector<AOTPointwiseSlot>& slots) {
  if (allocator == nullptr || source == nullptr || !IsProcessor<TextureEffect>(source.get()) ||
      slots.size() > MaxPointwiseSlots) {
    return nullptr;
  }
  auto* texture = static_cast<const TextureEffect*>(source.get());
  if (!texture->isYUV()) {
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
  return allocator->make<AOTYUVChainProcessor>(std::move(source), slots);
}

AOTYUVChainProcessor::AOTYUVChainProcessor(PlacementPtr<FragmentProcessor> source,
                                           const std::vector<AOTPointwiseSlot>& slots)
    : FragmentProcessor(ClassID()), slotCount(slots.size()) {
  for (size_t index = 0; index < slots.size(); ++index) {
    pointwiseSlots[index] = slots[index];
  }
  registerChildProcessor(std::move(source));
}

void AOTYUVChainProcessor::emitCode(EmitArgs& args) const {
  emitChild(0, args.inputColor, args);
  EmitAOTPointwiseSlots(args, pointwiseSlots.data(), slotCount, MaxPointwiseSlots, "yuv");
}

void AOTYUVChainProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // The operator structure travels in per-draw uniforms on the AOT path (the slot arrays the
  // kernel reads), so the artifact key needs none of it. The JIT path is different: emitCode()
  // bakes a different code structure per slot type (and per color-space step set), so two
  // AOTYUVChainProcessors over the same YUV source with different slots must not share a JIT
  // program — mirror the tail processor's key shape here (slot count, every slot's type, and
  // the color-space step key) or a hot cache hit replays the first structure's baked code.
  static constexpr uint32_t YUVChainTag = 0xA07C594Eu;
  bytesKey->write(YUVChainTag);
  bytesKey->write(static_cast<uint32_t>(slotCount));
  for (size_t index = 0; index < slotCount; ++index) {
    bytesKey->write(static_cast<uint32_t>(pointwiseSlots[index].type));
    if (pointwiseSlots[index].type == AOTPointwiseOpType::ColorSpaceXform) {
      bytesKey->write(
          ColorSpaceXformSteps::XFormKey(pointwiseSlots[index].colorSpaceXform.steps.get()));
    }
  }
}

void AOTYUVChainProcessor::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  if (fragmentUniformData == nullptr) {
    return;
  }
  fragmentUniformData->setDataOptional("PointwiseSlotCount", static_cast<int>(MaxPointwiseSlots));
  for (size_t index = 0; index < MaxPointwiseSlots; ++index) {
    UploadAOTPointwiseSlot(fragmentUniformData, index, MaxPointwiseSlots,
                           index < slotCount ? pointwiseSlots[index] : AOTPointwiseSlot{});
  }
}

}  // namespace tgfx
