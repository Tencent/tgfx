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

#include "GLSLQuadPerEdgeAAGeometryProcessor.h"

namespace tgfx {
PlacementPtr<QuadPerEdgeAAGeometryProcessor> QuadPerEdgeAAGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, AAType aa, std::optional<PMColor> commonColor,
    std::optional<Matrix> uvMatrix, bool hasSubset) {
  return allocator->make<GLSLQuadPerEdgeAAGeometryProcessor>(width, height, aa, commonColor,
                                                             uvMatrix, hasSubset);
}

GLSLQuadPerEdgeAAGeometryProcessor::GLSLQuadPerEdgeAAGeometryProcessor(
    int width, int height, AAType aa, std::optional<PMColor> commonColor,
    std::optional<Matrix> uvMatrix, bool hasSubset)
    : QuadPerEdgeAAGeometryProcessor(width, height, aa, commonColor, uvMatrix, hasSubset) {
}

void GLSLQuadPerEdgeAAGeometryProcessor::setData(UniformData* vertexUniformData,
                                                 UniformData* fragmentUniformData,
                                                 FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix.value_or(Matrix::I()), vertexUniformData, transformIter);
  if (vertexUniformData != nullptr && vertexUniformData->hasField("Matrix")) {
    // Merged vertex shaders always multiply positions by the Matrix uniform. These vertices are
    // already in device space, so identity keeps the result bit-exact.
    vertexUniformData->setData("Matrix", Matrix::I());
  }
  if (fragmentUniformData != nullptr && fragmentUniformData->hasField("HasCommonColor")) {
    // Precompiled fill kernels read the exact Color uniform for uniform-color draws instead of the
    // quantized per-vertex color; this flag selects the color source at runtime. Only precompiled
    // kernels declare the field, so the runtime (JIT) path is unaffected.
    fragmentUniformData->setData("HasCommonColor", commonColor.has_value() ? 1 : 0);
  }
  if (commonColor.has_value() && fragmentUniformData != nullptr &&
      fragmentUniformData->hasField("Color")) {
    // The fill kernels take the broadcast per-vertex color instead, so a program with no fragment
    // uniform block (plain color fill) skips this write entirely.
    fragmentUniformData->setData("Color", *commonColor);
  }
}

void GLSLQuadPerEdgeAAGeometryProcessor::onSetTransformData(UniformData* uniformData,
                                                            const CoordTransform* coordTransform,
                                                            int index) const {
  if (index == 0 && !subset.empty() && uvCoord.empty()) {
    // Subset only applies to the first image in ProgramInfo.
    uniformData->setData("texSubsetMatrix", coordTransform->getTotalMatrix());
  }
}

}  // namespace tgfx
