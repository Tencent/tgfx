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

namespace tgfx {
namespace {
static void UploadSlot(UniformData* uniformData, size_t index, const AOTPointwiseSlot& slot) {
  auto prefix = "Slot" + std::to_string(index);
  uniformData->setDataOptional(prefix + "OpType", static_cast<int>(slot.type));
  if (slot.type == AOTPointwiseOpType::ColorMatrix) {
    const auto& matrix = slot.colorMatrix.matrix;
    float matrixData[] = {
        matrix[0], matrix[5], matrix[10], matrix[15], matrix[1], matrix[6], matrix[11], matrix[16],
        matrix[2], matrix[7], matrix[12], matrix[17], matrix[3], matrix[8], matrix[13], matrix[18],
    };
    float vectorData[] = {matrix[4], matrix[9], matrix[14], matrix[19]};
    uniformData->setDataOptional(prefix + "ColorMatrix", matrixData);
    uniformData->setDataOptional(prefix + "ColorVector", vectorData);
  } else if (slot.type == AOTPointwiseOpType::Luma) {
    uniformData->setDataOptional(prefix + "Kr", slot.luma.kr);
    uniformData->setDataOptional(prefix + "Kg", slot.luma.kg);
    uniformData->setDataOptional(prefix + "Kb", slot.luma.kb);
  } else if (slot.type == AOTPointwiseOpType::AlphaThreshold) {
    uniformData->setDataOptional(prefix + "Threshold", slot.alphaThreshold.threshold);
  } else if (slot.type == AOTPointwiseOpType::ColorSpaceXform) {
    // Reuse the helper so this slot's transfer functions, gamut matrix and step flags are written
    // exactly as the standalone ColorSpaceXformEffect writes them, keeping AOT and JIT in agreement.
    ColorSpaceXformHelper helper(prefix);
    helper.setData(uniformData, slot.colorSpaceXform.steps.get());
    // setData writes OpType=3 through the same prefix, so the slot selector stays consistent.
  }
}
}  // namespace

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
  for (size_t index = 0; index < _slotCount; ++index) {
    const auto& current = slots[index];
    auto prefix = "Slot" + std::to_string(index);
    if (current.type == AOTPointwiseOpType::ColorMatrix) {
      auto matrix = args.uniformHandler->addUniform(prefix + "ColorMatrix", UniformFormat::Float4x4,
                                                    ShaderStage::Fragment);
      auto vector = args.uniformHandler->addUniform(prefix + "ColorVector", UniformFormat::Float4,
                                                    ShaderStage::Fragment);
      args.fragBuilder->codeAppendf("%s = vec4(%s.rgb / max(%s.a, 9.9999997473787516e-05), %s.a);",
                                    args.outputColor.c_str(), args.outputColor.c_str(),
                                    args.outputColor.c_str(), args.outputColor.c_str());
      args.fragBuilder->codeAppendf("%s = clamp(%s * %s + %s, 0.0, 1.0);", args.outputColor.c_str(),
                                    matrix.c_str(), args.outputColor.c_str(), vector.c_str());
      args.fragBuilder->codeAppendf("%s.rgb *= %s.a;", args.outputColor.c_str(),
                                    args.outputColor.c_str());
    } else if (current.type == AOTPointwiseOpType::Luma) {
      auto kr = args.uniformHandler->addUniform(prefix + "Kr", UniformFormat::Float,
                                                ShaderStage::Fragment);
      auto kg = args.uniformHandler->addUniform(prefix + "Kg", UniformFormat::Float,
                                                ShaderStage::Fragment);
      auto kb = args.uniformHandler->addUniform(prefix + "Kb", UniformFormat::Float,
                                                ShaderStage::Fragment);
      auto luma = "pointwiseTailLuma" + std::to_string(index);
      args.fragBuilder->codeAppendf("float %s = dot(%s.rgb, vec3(%s, %s, %s));", luma.c_str(),
                                    args.outputColor.c_str(), kr.c_str(), kg.c_str(), kb.c_str());
      args.fragBuilder->codeAppendf("%s = vec4(%s);", args.outputColor.c_str(), luma.c_str());
    } else if (current.type == AOTPointwiseOpType::AlphaThreshold) {
      auto threshold = args.uniformHandler->addUniform(prefix + "Threshold", UniformFormat::Float,
                                                       ShaderStage::Fragment);
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
      ColorSpaceXformHelper helper(prefix);
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
  for (size_t index = 0; index < MaxSlots; ++index) {
    UploadSlot(fragmentUniformData, index, slots[index]);
  }
}

}  // namespace tgfx
