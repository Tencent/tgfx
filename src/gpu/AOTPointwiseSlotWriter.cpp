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
#include "gpu/ColorSpaceXformHelper.h"

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

}  // namespace tgfx
