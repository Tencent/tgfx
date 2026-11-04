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

#include "GLSLGlassShapeGeometryFragmentProcessor.h"
#include <cmath>

namespace tgfx {

PlacementPtr<GlassSDFGeometryFragmentProcessor> GlassSDFGeometryFragmentProcessor::Make(
    BlockAllocator* allocator, GlassShapeType shapeType, const GlassSDFGeometryParams& params) {
  if (allocator == nullptr || shapeType == GlassShapeType::AlphaMask) {
    return nullptr;
  }
  return allocator->make<GLSLGlassSDFGeometryFragmentProcessor>(shapeType, params);
}

GLSLGlassSDFGeometryFragmentProcessor::GLSLGlassSDFGeometryFragmentProcessor(
    GlassShapeType shapeType, const GlassSDFGeometryParams& params)
    : GlassSDFGeometryFragmentProcessor(shapeType, params) {
}

void GLSLGlassSDFGeometryFragmentProcessor::onSetData(UniformData*,
                                                      UniformData* fragmentUniformData) const {
  float shapeData[4] = {params.halfW, params.halfH, params.cornerRadius, params.glassThickness};
  fragmentUniformData->setData("GlassShapeP0", shapeData);
  float effectData[4] = {params.refractionFactor, params.splay, params.depthRatio,
                         params.edgeBandLayerPixels};
  fragmentUniformData->setData("GlassShapeP1", effectData);
  float angle = params.lightAngle * static_cast<float>(M_PI) / 180.0f;
  float lightDirData[2] = {std::sin(angle), std::cos(angle)};
  fragmentUniformData->setData("GlassLightDir", lightDirData);
}

PlacementPtr<GlassUDFGeometryFragmentProcessor> GlassUDFGeometryFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> mask,
    std::shared_ptr<TextureProxy> edgeMask, const GlassUDFGeometryParams& params,
    bool enableEdgeLighting) {
  if (allocator == nullptr || mask == nullptr) {
    return nullptr;
  }
  if (enableEdgeLighting && edgeMask == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassUDFGeometryFragmentProcessor>(
      std::move(mask), std::move(edgeMask), params, enableEdgeLighting);
}

GLSLGlassUDFGeometryFragmentProcessor::GLSLGlassUDFGeometryFragmentProcessor(
    std::shared_ptr<TextureProxy> mask, std::shared_ptr<TextureProxy> edgeMask,
    const GlassUDFGeometryParams& params, bool enableEdgeLighting)
    : GlassUDFGeometryFragmentProcessor(std::move(mask), std::move(edgeMask), params,
                                        enableEdgeLighting) {
}

void GLSLGlassUDFGeometryFragmentProcessor::onSetData(UniformData*,
                                                      UniformData* fragmentUniformData) const {
  float shapeData[4] = {params.halfW, params.halfH, params.udfPixelToLayerPixelX,
                        params.udfPixelToLayerPixelY};
  fragmentUniformData->setData("GlassShapeP0", shapeData);
  float effectData[4] = {params.refractionFactor, params.splay, params.depthRatio,
                         params.edgeBandLayerPixels};
  fragmentUniformData->setData("GlassShapeP1", effectData);
  float angle = params.lightAngle * static_cast<float>(M_PI) / 180.0f;
  float lightDirData[2] = {std::sin(angle), std::cos(angle)};
  fragmentUniformData->setData("GlassLightDir", lightDirData);
  float edgeSpanData[2] = {params.edgeSpanX, params.edgeSpanY};
  fragmentUniformData->setData("GlassEdgeSpan", edgeSpanData);
  float textureWidth = static_cast<float>(maskProxy->width());
  float textureHeight = static_cast<float>(maskProxy->height());
  float coreWidth = std::round(params.halfW * 2.0f / params.udfPixelToLayerPixelX);
  float coreHeight = std::round(params.halfH * 2.0f / params.udfPixelToLayerPixelY);
  float scaleX = coreWidth / textureWidth;
  float scaleY = coreHeight / textureHeight;
  float maskUVData[4] = {scaleX, scaleY, -params.textureOriginX / textureWidth,
                         -params.textureOriginY / textureHeight};
  fragmentUniformData->setData("GlassFineMaskUV", maskUVData);
  float edgeScaleX = 1.0f;
  float edgeScaleY = 1.0f;
  float edgeOriginX = params.edgeTextureOriginX;
  float edgeOriginY = params.edgeTextureOriginY;
  if (enableEdgeLighting && edgeMaskProxy != nullptr) {
    float edgeTextureWidth = static_cast<float>(edgeMaskProxy->width());
    float edgeTextureHeight = static_cast<float>(edgeMaskProxy->height());
    float edgeCoreWidth = std::round(params.halfW * 2.0f / params.edgePixelToLayerPixelX);
    float edgeCoreHeight = std::round(params.halfH * 2.0f / params.edgePixelToLayerPixelY);
    edgeScaleX = edgeCoreWidth / edgeTextureWidth;
    edgeScaleY = edgeCoreHeight / edgeTextureHeight;
    edgeOriginX = -params.edgeTextureOriginX / edgeTextureWidth;
    edgeOriginY = -params.edgeTextureOriginY / edgeTextureHeight;
  }
  float edgeMaskUVData[4] = {edgeScaleX, edgeScaleY, edgeOriginX, edgeOriginY};
  fragmentUniformData->setData("GlassEdgeMaskUV", edgeMaskUVData);
  // Precompiled-only field: the merged UDF variant gates its edge-mask taps on this uniform (an
  // edge-lit-off draw binds the dummy texture on the edge slot and never samples it). Absent from
  // runtime programs, which emit the edge path or not at compile time.
  if (fragmentUniformData->hasField("EdgeMaskOn")) {
    fragmentUniformData->setData("EdgeMaskOn",
                                 enableEdgeLighting && edgeMaskProxy != nullptr ? 1 : 0);
  }
}

}  // namespace tgfx
