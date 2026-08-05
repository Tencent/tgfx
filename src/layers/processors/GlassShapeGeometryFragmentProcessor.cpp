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
    GlassShapeType shapeType, const GlassSDFGeometryParams& params)
    : GlassShapeGeometryFragmentProcessor(ClassID()), shapeType(shapeType), params(params) {
}

void GlassSDFGeometryFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(shapeType));
}

GlassUDFGeometryFragmentProcessor::GlassUDFGeometryFragmentProcessor(
    std::shared_ptr<TextureProxy> mask, const GlassUDFGeometryParams& params,
    bool enableEdgeLighting)
    : GlassShapeGeometryFragmentProcessor(ClassID()), maskProxy(std::move(mask)), params(params),
      enableEdgeLighting(enableEdgeLighting) {
}

void GlassUDFGeometryFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(enableEdgeLighting));
}

size_t GlassUDFGeometryFragmentProcessor::onCountTextureSamplers() const {
  return 1;
}

std::shared_ptr<Texture> GlassUDFGeometryFragmentProcessor::onTextureAt(size_t) const {
  if (maskProxy == nullptr) {
    return nullptr;
  }
  auto textureView = maskProxy->getTextureView();
  return textureView == nullptr ? nullptr : textureView->getTexture();
}

SamplerState GlassUDFGeometryFragmentProcessor::onSamplerStateAt(size_t) const {
  return SamplerState(TileMode::Decal, TileMode::Decal);
}

}  // namespace tgfx
