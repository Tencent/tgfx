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
//  unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLGlassRefractionFragmentProcessor.h"
#include <cmath>

namespace tgfx {

PlacementPtr<GlassRefractionFragmentProcessor> GlassRefractionFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
    PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry, const GlassRefractionParams& params,
    const Matrix& coordMatrix) {
  if (allocator == nullptr || source == nullptr || geometry == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassRefractionFragmentProcessor>(
      std::move(source), std::move(geometry), params, coordMatrix);
}

GLSLGlassRefractionFragmentProcessor::GLSLGlassRefractionFragmentProcessor(
    std::shared_ptr<TextureProxy> source,
    PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry, const GlassRefractionParams& params,
    const Matrix& coordMatrix)
    : GlassRefractionFragmentProcessor(std::move(source), std::move(geometry), params,
                                       coordMatrix) {
}

void GLSLGlassRefractionFragmentProcessor::onSetData(UniformData*,
                                                     UniformData* fragmentUniformData) const {
  float sourceWidth = static_cast<float>(sourceProxy->width());
  float sourceHeight = static_cast<float>(sourceProxy->height());
  float commonData[4] = {1.0f / sourceWidth, 1.0f / sourceHeight, params.dispersion,
                         params.maxDisplacement};
  fragmentUniformData->setData("GlassOpticsP0", commonData);

  float layerToSourceX = params.layerPixelToSourcePixelX;
  float layerToSourceY = params.layerPixelToSourcePixelY;
  if (layerToSourceX <= 0.0f) {
    layerToSourceX = params.origWidth > 0.0f ? sourceWidth / params.origWidth : 1.0f;
  }
  if (layerToSourceY <= 0.0f) {
    layerToSourceY = params.origHeight > 0.0f ? sourceHeight / params.origHeight : 1.0f;
  }
  float layerToSourceData[2] = {layerToSourceX / sourceWidth, layerToSourceY / sourceHeight};
  fragmentUniformData->setData("GlassOpticsP1", layerToSourceData);

  float glassUVScaleX = params.glassUVScaleX > 0.0f ? params.glassUVScaleX : 1.0f / sourceWidth;
  float glassUVScaleY = params.glassUVScaleY > 0.0f ? params.glassUVScaleY : 1.0f / sourceHeight;
  float offsetData[4] = {glassUVScaleX, glassUVScaleY, params.lightIntensity,
                         params.frost / 100.0f};
  fragmentUniformData->setData("GlassOpticsP2", offsetData);
  float geometryMappingData[4] = {params.glassUVOffsetX, params.glassUVOffsetY, 0.0f, 0.0f};
  fragmentUniformData->setData("GlassOpticsP3", geometryMappingData);
  // The precompiled refraction kernel folds the lighting branch into a runtime uniform, so it
  // rides this same onSetData path. Dispersion is a compile-time variant there (DISPERSION_ON),
  // matching the program-key predicate in onComputeProcessorKey, so it needs no uniform.
  if (fragmentUniformData->hasField("LightingOn")) {
    fragmentUniformData->setData("LightingOn", params.lightIntensity > 0.0f ? 1 : 0);
  }
  // The precompiled quad kernel's optional local shape mask defaults to off; the mask's coverage
  // processor, set after the color processors, turns it on. Absent from runtime programs.
  fragmentUniformData->setDataOptional("HasLocalMask", 0);
}

}  // namespace tgfx
