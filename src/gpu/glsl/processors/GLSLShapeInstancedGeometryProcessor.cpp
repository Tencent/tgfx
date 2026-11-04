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

#include "GLSLShapeInstancedGeometryProcessor.h"

namespace tgfx {
PlacementPtr<ShapeInstancedGeometryProcessor> ShapeInstancedGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, AAType aa, bool hasColors,
    const Matrix& uvMatrix, const Matrix& stateMatrix) {
  return allocator->make<GLSLShapeInstancedGeometryProcessor>(width, height, aa, hasColors,
                                                              uvMatrix, stateMatrix);
}

GLSLShapeInstancedGeometryProcessor::GLSLShapeInstancedGeometryProcessor(int width, int height,
                                                                         AAType aa, bool hasColors,
                                                                         const Matrix& uvMatrix,
                                                                         const Matrix& stateMatrix)
    : ShapeInstancedGeometryProcessor(width, height, aa, hasColors, uvMatrix, stateMatrix) {
}

void GLSLShapeInstancedGeometryProcessor::setData(UniformData* vertexUniformData, UniformData*,
                                                  FPCoordTransformIter* transformIter) const {
  vertexUniformData->setData("UVMatrix", uvMatrix);
  vertexUniformData->setData("ViewMatrix", viewMatrix);
  // For instanced rendering, emitTransforms uses local coords directly. The coord transform
  // matrices only need the FP's own transform since uvMatrix already recovers local coords.
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
}
}  // namespace tgfx
