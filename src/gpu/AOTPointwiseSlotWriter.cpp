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
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "AOTPointwiseSlotWriter.h"
#include <unordered_map>
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/glsl/GLSLBlend.h"
#include "gpu/processors/ConstColorProcessor.h"

namespace tgfx {

void UploadAOTPointwiseSlot(UniformData* uniformData, size_t slotIndex, uint32_t slotCapacity,
                            const AOTPointwiseSlot& slot) {
  uniformData->setArrayElementOptional("OpType", slotIndex, static_cast<int>(slot.type));
  if (slot.type == AOTPointwiseOpType::ColorMatrix) {
    const auto& matrix = slot.colorMatrix.matrix;
    float matrixData[] = {
        matrix[0], matrix[5], matrix[10], matrix[15], matrix[1], matrix[6], matrix[11], matrix[16],
        matrix[2], matrix[7], matrix[12], matrix[17], matrix[3], matrix[8], matrix[13], matrix[18],
    };
    float vectorData[] = {matrix[4], matrix[9], matrix[14], matrix[19]};
    uniformData->setArrayElementOptional("ColorMatrix", slotIndex, matrixData);
    uniformData->setArrayElementOptional("ColorVector", slotIndex, vectorData);
  } else if (slot.type == AOTPointwiseOpType::Luma) {
    uniformData->setArrayElementOptional("Kr", slotIndex, slot.luma.kr);
    uniformData->setArrayElementOptional("Kg", slotIndex, slot.luma.kg);
    uniformData->setArrayElementOptional("Kb", slotIndex, slot.luma.kb);
  } else if (slot.type == AOTPointwiseOpType::AlphaThreshold) {
    uniformData->setArrayElementOptional("Threshold", slotIndex, slot.alphaThreshold.threshold);
  } else if (slot.type == AOTPointwiseOpType::ColorSpaceXform) {
    // Reuse the helper so this slot's transfer functions, gamut matrix and step flags are written
    // exactly as the standalone ColorSpaceXformEffect writes them, keeping AOT and JIT in
    // agreement. The slot-mode helper also writes OpType=3 for this element.
    ColorSpaceXformHelper(static_cast<int>(slotIndex), slotCapacity)
        .setData(uniformData, slot.colorSpaceXform.steps.get());
  } else if (slot.type == AOTPointwiseOpType::ConstColor) {
    uniformData->setArrayElementOptional("ConstColorValue", slotIndex, slot.constColor.color);
    uniformData->setArrayElementOptional("ConstInputMode", slotIndex, slot.constColor.inputMode);
  } else if (slot.type == AOTPointwiseOpType::Blend) {
    uniformData->setArrayElementOptional("ConstColorValue", slotIndex, slot.constColor.color);
    uniformData->setArrayElementOptional("BlendModeValue", slotIndex, slot.blend.blendMode);
    uniformData->setArrayElementOptional("BlendConstFirst", slotIndex,
                                         slot.blend.childType == 1 ? 1 : 0);
  }
}

void EmitAOTPointwiseSlots(FragmentProcessor::EmitArgs& args, const AOTPointwiseSlot* slots,
                           size_t slotCount, uint32_t slotCapacity,
                           const std::string& localPrefix) {
  // Per-slot uniform fields are std140 arrays shared with the precompiled kernel; declare each
  // array once and reference its elements by slot index.
  std::unordered_map<std::string, std::string> declaredArrays = {};
  auto declareSlotField = [&](const char* field, UniformFormat format, size_t index) {
    auto [it, _] = declaredArrays.try_emplace(
        field, args.uniformHandler->addUniform(field, format, ShaderStage::Fragment, slotCapacity));
    return it->second + "[" + std::to_string(index) + "]";
  };
  for (size_t index = 0; index < slotCount; ++index) {
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
      auto luma = localPrefix + "Luma" + std::to_string(index);
      args.fragBuilder->codeAppendf("float %s = dot(%s.rgb, vec3(%s, %s, %s));", luma.c_str(),
                                    args.outputColor.c_str(), kr.c_str(), kg.c_str(), kb.c_str());
      args.fragBuilder->codeAppendf("%s = vec4(%s);", args.outputColor.c_str(), luma.c_str());
    } else if (current.type == AOTPointwiseOpType::AlphaThreshold) {
      auto threshold = declareSlotField("Threshold", UniformFormat::Float, index);
      auto stepped = localPrefix + "Step" + std::to_string(index);
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
    } else if (current.type == AOTPointwiseOpType::ConstColor) {
      auto value = declareSlotField("ConstColorValue", UniformFormat::Float4, index);
      // Mirrors GLSLConstColorProcessor's emission for the same input mode. The precompiled kernel
      // folds the mode into the ConstInputMode runtime uniform; the JIT bakes it per slot, so the
      // shared upload's ConstInputMode write is an optional no-op here.
      switch (static_cast<InputMode>(current.constColor.inputMode)) {
        case InputMode::Ignore:
          args.fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), value.c_str());
          break;
        case InputMode::ModulateRGBA:
          args.fragBuilder->codeAppendf("%s = %s * %s;", args.outputColor.c_str(), value.c_str(),
                                        args.inputColor.c_str());
          break;
        case InputMode::ModulateA:
          args.fragBuilder->codeAppendf("%s = %s * %s.a;", args.outputColor.c_str(), value.c_str(),
                                        args.inputColor.c_str());
          break;
      }
    } else if (current.type == AOTPointwiseOpType::Blend) {
      // Blend with a constant operand, matching the precompiled kernel's OP_BLEND: the operand
      // order follows BlendConstFirst, the mode is baked per slot (the shared upload's
      // BlendModeValue write is an optional no-op here).
      auto value = declareSlotField("ConstColorValue", UniformFormat::Float4, index);
      auto srcName = localPrefix + "BlendSrc" + std::to_string(index);
      auto dstName = localPrefix + "BlendDst" + std::to_string(index);
      // childType mirrors XfermodeFragmentProcessor::Child: SrcChild (1) puts the constant in the
      // src operand, DstChild (0) in the dst operand — same mapping as the shared upload's
      // BlendConstFirst bit.
      if (current.blend.childType == 1) {
        args.fragBuilder->codeAppendf("vec4 %s = %s;", srcName.c_str(), value.c_str());
        args.fragBuilder->codeAppendf("vec4 %s = %s;", dstName.c_str(), args.outputColor.c_str());
      } else {
        args.fragBuilder->codeAppendf("vec4 %s = %s;", srcName.c_str(), args.outputColor.c_str());
        args.fragBuilder->codeAppendf("vec4 %s = %s;", dstName.c_str(), value.c_str());
      }
      AppendMode(args.fragBuilder, srcName, "vec4(1.0)", dstName, args.outputColor,
                 static_cast<BlendMode>(current.blend.blendMode), false);
    } else {
      // Emit the color-space steps through the shared helper and builder routine so this slot's
      // generated code matches the standalone ColorSpaceXformEffect exactly.
      ColorSpaceXformHelper helper(static_cast<int>(index), slotCapacity);
      auto steps = current.colorSpaceXform.steps.get();
      helper.emitCode(args.uniformHandler, steps);
      std::string transformed;
      args.fragBuilder->appendColorGamutXform(&transformed, args.outputColor.c_str(), &helper);
      args.fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), transformed.c_str());
    }
  }
}

}  // namespace tgfx
