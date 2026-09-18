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
#include "gpu/ColorSpaceXformHelper.h"

namespace tgfx {

PlacementPtr<AOTYUVChainProcessor> AOTYUVChainProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> source,
    const std::vector<AOTPointwiseSlot>& slots) {
  if (allocator == nullptr || source == nullptr || source->name() != "TextureEffect" ||
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
    if (slot.type == AOTPointwiseOpType::ColorSpaceXform && slot.colorSpaceXform.steps == nullptr) {
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
  for (size_t index = 0; index < slotCount; ++index) {
    const auto& current = pointwiseSlots[index];
    if (current.type == AOTPointwiseOpType::ColorMatrix) {
      auto matrix = args.uniformHandler->addUniform("ColorMatrix", UniformFormat::Float4x4,
                                                    ShaderStage::Fragment, MaxPointwiseSlots);
      auto vector = args.uniformHandler->addUniform("ColorVector", UniformFormat::Float4,
                                                    ShaderStage::Fragment, MaxPointwiseSlots);
      args.fragBuilder->codeAppendf("%s = vec4(%s.rgb / max(%s.a, 9.9999997473787516e-05), %s.a);",
                                    args.outputColor.c_str(), args.outputColor.c_str(),
                                    args.outputColor.c_str(), args.outputColor.c_str());
      args.fragBuilder->codeAppendf("%s = clamp(%s[%d] * %s + %s[%d], 0.0, 1.0);",
                                    args.outputColor.c_str(), matrix.c_str(),
                                    static_cast<int>(index), args.outputColor.c_str(),
                                    vector.c_str(), static_cast<int>(index));
      args.fragBuilder->codeAppendf("%s.rgb *= %s.a;", args.outputColor.c_str(),
                                    args.outputColor.c_str());
    } else if (current.type == AOTPointwiseOpType::Luma) {
      auto kr = args.uniformHandler->addUniform("Kr", UniformFormat::Float, ShaderStage::Fragment,
                                                MaxPointwiseSlots);
      auto kg = args.uniformHandler->addUniform("Kg", UniformFormat::Float, ShaderStage::Fragment,
                                                MaxPointwiseSlots);
      auto kb = args.uniformHandler->addUniform("Kb", UniformFormat::Float, ShaderStage::Fragment,
                                                MaxPointwiseSlots);
      args.fragBuilder->codeAppendf("float yuvLuma%d = dot(%s.rgb, vec3(%s[%d], %s[%d], %s[%d]));",
                                    static_cast<int>(index), args.outputColor.c_str(), kr.c_str(),
                                    static_cast<int>(index), kg.c_str(), static_cast<int>(index),
                                    kb.c_str(), static_cast<int>(index));
      args.fragBuilder->codeAppendf("%s = vec4(yuvLuma%d);", args.outputColor.c_str(),
                                    static_cast<int>(index));
    } else if (current.type == AOTPointwiseOpType::AlphaThreshold) {
      auto threshold = args.uniformHandler->addUniform("Threshold", UniformFormat::Float,
                                                       ShaderStage::Fragment, MaxPointwiseSlots);
      args.fragBuilder->codeAppendf("vec4 yuvStep%d = vec4(0.0);", static_cast<int>(index));
      args.fragBuilder->codeAppendf("if (%s.a > 0.0) {", args.outputColor.c_str());
      args.fragBuilder->codeAppendf("  yuvStep%d.rgb = %s.rgb / %s.a;", static_cast<int>(index),
                                    args.outputColor.c_str(), args.outputColor.c_str());
      args.fragBuilder->codeAppendf("  yuvStep%d.a = step(%s[%d], %s.a);", static_cast<int>(index),
                                    threshold.c_str(), static_cast<int>(index),
                                    args.outputColor.c_str());
      args.fragBuilder->codeAppendf("  yuvStep%d = clamp(yuvStep%d, 0.0, 1.0);",
                                    static_cast<int>(index), static_cast<int>(index));
      args.fragBuilder->codeAppend("}");
      args.fragBuilder->codeAppendf("%s = yuvStep%d;", args.outputColor.c_str(),
                                    static_cast<int>(index));
    } else {
      // ColorSpaceXform rides the same ColorSpaceXformHelper path the tail processor uses;
      // slots beyond the count stay OP_NONE in the precompiled kernel.
      auto steps = current.colorSpaceXform.steps.get();
      ColorSpaceXformHelper helper(static_cast<int>(index), MaxPointwiseSlots);
      helper.emitCode(args.uniformHandler, steps);
      std::string transformed;
      args.fragBuilder->appendColorGamutXform(&transformed, args.outputColor.c_str(), &helper);
      args.fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), transformed.c_str());
    }
  }
}

void AOTYUVChainProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // The operator structure travels in per-draw uniforms (the slot arrays the kernel reads);
  // the source child's own key (plane format, sampler flags) rides the base class's child-key
  // walk, so two grades over the same YUV source share one program.
  static constexpr uint32_t YUVChainTag = 0xA07C594Eu;
  bytesKey->write(YUVChainTag);
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
