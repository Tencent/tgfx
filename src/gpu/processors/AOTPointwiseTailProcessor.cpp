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
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/processors/AOTPointwiseSlotWriter.h"

namespace tgfx {
namespace {}  // namespace

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
    if (slot.type == AOTPointwiseOpType::ColorSpaceXform && slot.colorSpaceXform.steps == nullptr) {
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
  // Per-slot uniform fields are std140 arrays shared with the precompiled kernel; declare each
  // array once and reference its elements by slot index.
  std::unordered_map<std::string, std::string> declaredArrays = {};
  auto declareSlotField = [&](const char* field, UniformFormat format, size_t index) {
    auto [it, _] = declaredArrays.try_emplace(
        field, args.uniformHandler->addUniform(field, format, ShaderStage::Fragment, MaxSlots));
    return it->second + "[" + std::to_string(index) + "]";
  };
  for (size_t index = 0; index < _slotCount; ++index) {
    const auto& current = slots[index];
    if (current.type == AOTPointwiseOpType::ColorMatrix) {
      auto matrix = declareSlotField("ColorMatrix", UniformFormat::Float4x4, index);
      auto vector = declareSlotField("ColorVector", UniformFormat::Float4, index);
      args.fragBuilder->codeAppendf("%s = vec4(%s.rgb / max(%s.a, 9.9999997473787516e-05), %s.a);",
                                    args.outputColor.c_str(), args.outputColor.c_str(),
                                    args.outputColor.c_str(), args.outputColor.c_str());
      args.fragBuilder->codeAppendf("%s = clamp(%s * %s + %s, 0.0, 1.0);", args.outputColor.c_str(),
                                    matrix.c_str(), args.outputColor.c_str(), vector.c_str());
      args.fragBuilder->codeAppendf("%s.rgb *= %s.a;", args.outputColor.c_str(),
                                    args.outputColor.c_str());
    } else if (current.type == AOTPointwiseOpType::Luma) {
      auto kr = declareSlotField("Kr", UniformFormat::Float, index);
      auto kg = declareSlotField("Kg", UniformFormat::Float, index);
      auto kb = declareSlotField("Kb", UniformFormat::Float, index);
      auto luma = "pointwiseTailLuma" + std::to_string(index);
      args.fragBuilder->codeAppendf("float %s = dot(%s.rgb, vec3(%s, %s, %s));", luma.c_str(),
                                    args.outputColor.c_str(), kr.c_str(), kg.c_str(), kb.c_str());
      args.fragBuilder->codeAppendf("%s = vec4(%s);", args.outputColor.c_str(), luma.c_str());
    } else if (current.type == AOTPointwiseOpType::AlphaThreshold) {
      auto threshold = declareSlotField("Threshold", UniformFormat::Float, index);
      auto stepped = "pointwiseTailStep" + std::to_string(index);
      args.fragBuilder->codeAppendf("vec4 %s = vec4(0.0);", stepped.c_str());
      args.fragBuilder->codeAppendf("if (%s.a > 0.0) {", args.outputColor.c_str());
      args.fragBuilder->codeAppendf("  %s.rgb = %s.rgb / %s.a;", stepped.c_str(),
                                    args.outputColor.c_str(), args.outputColor.c_str());
      args.fragBuilder->codeAppendf("  %s.a = step(%s, %s.a);", stepped.c_str(), threshold.c_str(),
                                    args.outputColor.c_str());
      args.fragBuilder->codeAppendf("  %s = clamp(%s, 0.0, 1.0);", stepped.c_str(),
                                    stepped.c_str());
      args.fragBuilder->codeAppend("}");
      args.fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), stepped.c_str());
    } else {
      // Emit the color-space steps through the shared helper and builder routine so this slot's
      // generated code matches the standalone ColorSpaceXformEffect exactly.
      ColorSpaceXformHelper helper(static_cast<int>(index), MaxSlots);
      auto steps = current.colorSpaceXform.steps.get();
      helper.emitCode(args.uniformHandler, steps);
      std::string transformed;
      args.fragBuilder->appendColorGamutXform(&transformed, args.outputColor.c_str(), &helper);
      args.fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), transformed.c_str());
    }
  }
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
