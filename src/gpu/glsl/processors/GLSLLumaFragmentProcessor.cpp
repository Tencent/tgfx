/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GLSLLumaFragmentProcessor.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> LumaFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<ColorSpace> colorSpace) {
  return allocator->make<GLSLLumaFragmentProcessor>(std::move(colorSpace));
}

PlacementPtr<FragmentProcessor> LumaFragmentProcessor::Make(BlockAllocator* allocator, float kr,
                                                            float kg, float kb) {
  return allocator->make<GLSLLumaFragmentProcessor>(kr, kg, kb);
}

void GLSLLumaFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  fragmentUniformData->setArrayElement("Kr", 0, _lumaFactor.kr);
  fragmentUniformData->setArrayElement("Kg", 0, _lumaFactor.kg);
  fragmentUniformData->setArrayElement("Kb", 0, _lumaFactor.kb);
  // Select the Luma operator in the chain interpreter's slot 0 (the only kernel that carries
  // the operator word now that the single-operator family is gone).
  fragmentUniformData->setArrayElementOptional("OpType", 0, 1);
}

}  // namespace tgfx
