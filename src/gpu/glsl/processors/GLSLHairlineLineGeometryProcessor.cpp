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

#include "GLSLHairlineLineGeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {

PlacementPtr<HairlineLineGeometryProcessor> HairlineLineGeometryProcessor::Make(
    BlockAllocator* allocator, const PMColor& color, const Matrix& viewMatrix,
    std::optional<Matrix> uvMatrix, float coverage, AAType aaType) {
  return allocator->make<GLSLHairlineLineGeometryProcessor>(color, viewMatrix, uvMatrix, coverage,
                                                            aaType);
}

GLSLHairlineLineGeometryProcessor::GLSLHairlineLineGeometryProcessor(const PMColor& color,
                                                                     const Matrix& viewMatrix,
                                                                     std::optional<Matrix> uvMatrix,
                                                                     float coverage, AAType aaType)
    : HairlineLineGeometryProcessor(color, viewMatrix, uvMatrix, coverage, aaType) {
}

void GLSLHairlineLineGeometryProcessor::setData(UniformData* vertexUniformData,
                                                UniformData* fragmentUniformData,
                                                FPCoordTransformIter* transformIter) const {
  if (uvMatrix.has_value()) {
    setTransformDataHelper(*uvMatrix, vertexUniformData, transformIter);
  }
  // The hairline color is always the common paint color: the precompiled kernels read the exact
  // Color uniform through the HasCommonColor runtime uniform instead of the white filler in the
  // shared vertex buffers. Only precompiled kernels declare the field.
  if (fragmentUniformData != nullptr && fragmentUniformData->hasField("HasCommonColor")) {
    fragmentUniformData->setData("HasCommonColor", 1);
  }
  fragmentUniformData->setData("Color", commonColor);
  vertexUniformData->setData("Matrix", viewMatrix);
  fragmentUniformData->setData("Coverage", coverage);
  int aaEnabled = aaType == AAType::Coverage ? 1 : 0;
  fragmentUniformData->setData("AAEnabled", aaEnabled);
}

}  // namespace tgfx
