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

#include "GLSLColorMatrixFragmentProcessor.h"

namespace tgfx {
PlacementPtr<ColorMatrixFragmentProcessor> ColorMatrixFragmentProcessor::Make(
    BlockAllocator* allocator, const std::array<float, 20>& matrix) {
  return allocator->make<GLSLColorMatrixFragmentProcessor>(matrix);
}

GLSLColorMatrixFragmentProcessor::GLSLColorMatrixFragmentProcessor(
    const std::array<float, 20>& matrix)
    : ColorMatrixFragmentProcessor(matrix) {
}

void GLSLColorMatrixFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                 UniformData* fragmentUniformData) const {
  float m[] = {
      matrix[0], matrix[5], matrix[10], matrix[15], matrix[1], matrix[6], matrix[11], matrix[16],
      matrix[2], matrix[7], matrix[12], matrix[17], matrix[3], matrix[8], matrix[13], matrix[18],
  };
  float vec[] = {
      matrix[4],
      matrix[9],
      matrix[14],
      matrix[19],
  };
  fragmentUniformData->setData("Matrix", m);
  fragmentUniformData->setData("Vector", vec);
}
}  // namespace tgfx
