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

#include "GLSLMeshGeometryProcessor.h"

namespace tgfx {

PlacementPtr<MeshGeometryProcessor> MeshGeometryProcessor::Make(BlockAllocator* allocator,
                                                                bool hasTexCoords, bool hasColors,
                                                                bool hasCoverage, PMColor color,
                                                                const Matrix& viewMatrix) {
  return allocator->make<GLSLMeshGeometryProcessor>(hasTexCoords, hasColors, hasCoverage, color,
                                                    viewMatrix);
}

GLSLMeshGeometryProcessor::GLSLMeshGeometryProcessor(bool hasTexCoords, bool hasColors,
                                                     bool hasCoverage, PMColor color,
                                                     const Matrix& viewMatrix)
    : MeshGeometryProcessor(hasTexCoords, hasColors, hasCoverage, color, viewMatrix) {
}

void GLSLMeshGeometryProcessor::setData(UniformData* vertexUniformData,
                                        UniformData* fragmentUniformData,
                                        FPCoordTransformIter* transformIter) const {
  vertexUniformData->setData("Matrix", viewMatrix);

  // For mesh, position is already in local coordinate space (not pre-transformed like shape).
  // Both hasTexCoords and !hasTexCoords cases use identity since coordinates don't need
  // additional transformation - they're already suitable for shader sampling.
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);

  // The Color uniform is the exact paint color for meshes without per-vertex colors (the baked
  // white attribute slot cannot hold the per-draw color); meshes with per-vertex colors read the
  // vColor varying instead. The precompiled MeshFillShader declares both plus the HasCommonColor
  // runtime uniform that selects the source. The JIT program with per-vertex colors declares no
  // fragment uniform at all, hence the null guard and the optional writes.
  if (fragmentUniformData != nullptr) {
    fragmentUniformData->setDataOptional("Color", commonColor);
    fragmentUniformData->setDataOptional("HasCommonColor", hasColors ? 0 : 1);
  }
}

}  // namespace tgfx
