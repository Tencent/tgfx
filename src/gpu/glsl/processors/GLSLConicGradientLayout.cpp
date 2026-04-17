/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GLSLConicGradientLayout.h"

namespace tgfx {
PlacementPtr<ConicGradientLayout> ConicGradientLayout::Make(BlockAllocator* allocator,
                                                            Matrix matrix, float bias,
                                                            float scale) {
  return allocator->make<GLSLConicGradientLayout>(matrix, bias, scale);
}

GLSLConicGradientLayout::GLSLConicGradientLayout(Matrix matrix, float bias, float scale)
    : ConicGradientLayout(matrix, bias, scale) {
}

void GLSLConicGradientLayout::onSetData(UniformData* /*vertexUniformData*/,
                                        UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("Bias", bias);
  fragmentUniformData->setData("Scale", scale);
}
}  // namespace tgfx
