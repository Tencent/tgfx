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

#include "GLSLAtlasTextGeometryProcessor.h"

namespace tgfx {
PlacementPtr<AtlasTextGeometryProcessor> AtlasTextGeometryProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> textureProxy, AAType aa,
    std::optional<PMColor> commonColor, const SamplingOptions& sampling) {
  return allocator->make<GLSLAtlasTextGeometryProcessor>(std::move(textureProxy), aa, commonColor,
                                                         sampling);
}

GLSLAtlasTextGeometryProcessor::GLSLAtlasTextGeometryProcessor(
    std::shared_ptr<TextureProxy> textureProxy, AAType aa, std::optional<PMColor> commonColor,
    const SamplingOptions& sampling)
    : AtlasTextGeometryProcessor(std::move(textureProxy), aa, commonColor, sampling) {
}

void GLSLAtlasTextGeometryProcessor::setData(UniformData* vertexUniformData,
                                             UniformData* fragmentUniformData,
                                             FPCoordTransformIter* transformIter) const {
  auto atlasSizeInv = textureProxy->getTextureView()->getTextureCoord(1.f, 1.f);
  // The dedicated kernel declares atlasSizeInv but no Matrix; the chain kernel declares Matrix
  // (identity here: atlas positions are already in device space) but no atlasSizeInv.
  vertexUniformData->setDataOptional(atlasSizeUniformName, atlasSizeInv);
  if (vertexUniformData->hasField("Matrix")) {
    vertexUniformData->setData("Matrix", Matrix::I());
  }
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  // The precompiled kernels always bind the color attribute; common-color draws read the exact
  // Color uniform instead of the 8-bit quantized broadcast, selected by HasCommonColor. Only
  // precompiled kernels declare the field.
  if (fragmentUniformData != nullptr) {
    if (commonColor.has_value()) {
      fragmentUniformData->setDataOptional("Color", *commonColor);
    }
    if (fragmentUniformData->hasField("HasCommonColor")) {
      fragmentUniformData->setData("HasCommonColor", commonColor.has_value() ? 1 : 0);
    }
  }
  if (fragmentUniformData != nullptr && fragmentUniformData->hasField("AlphaOnly")) {
    // Precompiled variants fold ALPHA_ONLY into a runtime uniform; the JIT program bakes it into
    // emitCode and, for the no-common-color / no-XP case, emits no fragment uniform block at all
    // (fragmentUniformData is null there), so both the null and hasField guards are required.
    int alphaOnly = isAlphaOnly() ? 1 : 0;
    fragmentUniformData->setData("AlphaOnly", alphaOnly);
  }
  if (fragmentUniformData != nullptr) {
    // Default for the runtime device mask; a present mask FP overwrites it with 1 later.
    fragmentUniformData->setDataOptional("HasDeviceMask", 0);
  }
}
}  // namespace tgfx
