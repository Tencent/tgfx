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

#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/AOTEffect.h"

namespace tgfx {

bool ColorMatrixFragmentProcessor::isChannelPermutation() const {
  bool usedInput[3] = {false, false, false};
  for (size_t row = 0; row < 3; ++row) {
    size_t selectedInput = 3;
    for (size_t column = 0; column < 3; ++column) {
      auto value = matrix[row * 5 + column];
      if (value == 1.0f && selectedInput == 3) {
        selectedInput = column;
      } else if (value != 0.0f) {
        return false;
      }
    }
    if (selectedInput == 3 || usedInput[selectedInput] || matrix[row * 5 + 3] != 0.0f ||
        matrix[row * 5 + 4] != 0.0f) {
      return false;
    }
    usedInput[selectedInput] = true;
  }
  return matrix[15] == 0.0f && matrix[16] == 0.0f && matrix[17] == 0.0f && matrix[18] == 1.0f &&
         matrix[19] == 0.0f;
}

void ColorMatrixFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(isChannelPermutation()));
}

bool ColorMatrixFragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                              AOTNodeID* output) const {
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  AOTColorMatrixParameters parameters = {};
  parameters.matrix = matrix;
  return builder->addColorMatrix(input, parameters, output);
}

}  // namespace tgfx
