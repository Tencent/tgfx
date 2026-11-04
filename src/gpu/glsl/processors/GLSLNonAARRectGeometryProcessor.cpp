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

#include "GLSLNonAARRectGeometryProcessor.h"

namespace tgfx {
PlacementPtr<NonAARRectGeometryProcessor> NonAARRectGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLNonAARRectGeometryProcessor>(width, height, stroke, commonColor);
}

GLSLNonAARRectGeometryProcessor::GLSLNonAARRectGeometryProcessor(int width, int height, bool stroke,
                                                                 std::optional<PMColor> commonColor)
    : NonAARRectGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLNonAARRectGeometryProcessor::setData(UniformData* vertexUniformData,
                                              UniformData* fragmentUniformData,
                                              FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (fragmentUniformData != nullptr) {
    // The precompiled kernels always bind the color attribute; common-color draws read the exact
    // Color uniform instead of the 8-bit quantized broadcast, selected by HasCommonColor. Only
    // precompiled kernels declare the field.
    if (commonColor.has_value()) {
      fragmentUniformData->setDataOptional("Color", *commonColor);
    }
    if (fragmentUniformData->hasField("HasCommonColor")) {
      fragmentUniformData->setData("HasCommonColor", commonColor.has_value() ? 1 : 0);
    }
  }
}
}  // namespace tgfx
