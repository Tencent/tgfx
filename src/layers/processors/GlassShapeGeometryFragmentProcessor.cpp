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

#include "layers/processors/GlassShapeGeometryFragmentProcessor.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

GlassSDFGeometryFragmentProcessor::GlassSDFGeometryFragmentProcessor(
    GlassShapeType shapeType, const GlassShapeGeometryParams& params, float sourceWidth,
    float sourceHeight)
    : GlassShapeGeometryFragmentProcessor(ClassID()), shapeType(shapeType), params(params),
      sourceWidth(sourceWidth), sourceHeight(sourceHeight) {
}

void GlassSDFGeometryFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(shapeType));
}

GlassUDFGeometryFragmentProcessor::GlassUDFGeometryFragmentProcessor(
    std::shared_ptr<TextureProxy> fineMask, std::shared_ptr<TextureProxy> coarseMask,
    const GlassShapeGeometryParams& params, float sourceWidth, float sourceHeight,
    bool enableEdgeLighting)
    : GlassShapeGeometryFragmentProcessor(ClassID()), fineMaskProxy(std::move(fineMask)),
      coarseMaskProxy(std::move(coarseMask)), params(params), sourceWidth(sourceWidth),
      sourceHeight(sourceHeight), enableEdgeLighting(enableEdgeLighting) {
}

void GlassUDFGeometryFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(coarseMaskProxy != nullptr && enableEdgeLighting));
}

size_t GlassUDFGeometryFragmentProcessor::onCountTextureSamplers() const {
  return coarseMaskProxy == nullptr ? 1 : 2;
}

std::shared_ptr<Texture> GlassUDFGeometryFragmentProcessor::onTextureAt(size_t index) const {
  const auto& proxy = index == 0 ? fineMaskProxy : coarseMaskProxy;
  if (proxy == nullptr) {
    return nullptr;
  }
  auto textureView = proxy->getTextureView();
  return textureView == nullptr ? nullptr : textureView->getTexture();
}

SamplerState GlassUDFGeometryFragmentProcessor::onSamplerStateAt(size_t) const {
  return SamplerState(TileMode::Decal, TileMode::Decal);
}

}  // namespace tgfx
