/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "GLSLAlphaThresholdFragmentProcessor.h"

namespace tgfx {
PlacementPtr<AlphaThresholdFragmentProcessor> AlphaThresholdFragmentProcessor::Make(
    BlockAllocator* allocator, float threshold) {
  return allocator->make<GLSLAlphaThresholdFragmentProcessor>(threshold);
}

void GLSLAlphaThresholdFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  fragmentUniformData->setArrayElement("Threshold", 0, threshold);
  // Select the AlphaThreshold operator in the chain interpreter's slot 0 (the only kernel that carries
  // the operator word now that the single-operator family is gone).
  fragmentUniformData->setArrayElementOptional("OpType", 0, 2);
}

}  // namespace tgfx
