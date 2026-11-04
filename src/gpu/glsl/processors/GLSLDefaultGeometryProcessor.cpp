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

#include "GLSLDefaultGeometryProcessor.h"

namespace tgfx {
PlacementPtr<DefaultGeometryProcessor> DefaultGeometryProcessor::Make(BlockAllocator* allocator,
                                                                      PMColor color, int width,
                                                                      int height, AAType aa,
                                                                      const Matrix& viewMatrix,
                                                                      const Matrix& uvMatrix) {
  return allocator->make<GLSLDefaultGeometryProcessor>(color, width, height, aa, viewMatrix,
                                                       uvMatrix);
}

GLSLDefaultGeometryProcessor::GLSLDefaultGeometryProcessor(PMColor color, int width, int height,
                                                           AAType aa, const Matrix& viewMatrix,
                                                           const Matrix& uvMatrix)
    : DefaultGeometryProcessor(color, width, height, aa, viewMatrix, uvMatrix) {
}

void GLSLDefaultGeometryProcessor::setData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix, vertexUniformData, transformIter);
  // The DefaultGP color is always the common paint color: the precompiled kernels read the exact
  // Color uniform through the HasCommonColor runtime uniform instead of the broadcast (white)
  // per-vertex slot, whose buffers are shared across draws. Only precompiled kernels declare the
  // field, so the JIT path is unaffected.
  if (fragmentUniformData != nullptr) {
    fragmentUniformData->setDataOptional("Color", commonColor);
    if (fragmentUniformData->hasField("HasCommonColor")) {
      fragmentUniformData->setData("HasCommonColor", 1);
    }
  }
  vertexUniformData->setData("Matrix", viewMatrix);
}
}  // namespace tgfx
