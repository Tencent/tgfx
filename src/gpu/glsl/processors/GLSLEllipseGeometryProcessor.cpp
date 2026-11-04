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

#include "GLSLEllipseGeometryProcessor.h"

namespace tgfx {
PlacementPtr<EllipseGeometryProcessor> EllipseGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLEllipseGeometryProcessor>(width, height, stroke, commonColor);
}

GLSLEllipseGeometryProcessor::GLSLEllipseGeometryProcessor(int width, int height, bool stroke,
                                                           std::optional<PMColor> commonColor)
    : EllipseGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLEllipseGeometryProcessor::setData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  // The precompiled kernels always bind the color attribute; common-color draws read the exact
  // Color uniform instead of the 8-bit quantized broadcast, selected by HasCommonColor. Only
  // precompiled kernels declare the field, so the JIT path (uniform when common, varying
  // otherwise) is unaffected.
  if (fragmentUniformData != nullptr) {
    if (commonColor.has_value()) {
      fragmentUniformData->setDataOptional("Color", *commonColor);
    }
    if (fragmentUniformData->hasField("HasCommonColor")) {
      fragmentUniformData->setData("HasCommonColor", commonColor.has_value() ? 1 : 0);
    }
  }
  int strokeEnabled = stroke ? 1 : 0;
  fragmentUniformData->setData("StrokeEnabled", strokeEnabled);
}
}  // namespace tgfx
