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

#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

GlassRefractionFragmentProcessor::GlassRefractionFragmentProcessor(
    std::shared_ptr<TextureProxy> source, std::shared_ptr<TextureProxy> fineMask,
    std::shared_ptr<TextureProxy> coarseMask, const GlassRefractionParams& params,
    const Matrix& coordMatrix)
    : FragmentProcessor(ClassID()), sourceProxy(std::move(source)),
      fineMaskProxy(std::move(fineMask)), coarseMaskProxy(std::move(coarseMask)), params(params),
      coordTransform(coordMatrix) {
  addCoordTransform(&coordTransform);
}

void GlassRefractionFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(params.shapeType));
  uint32_t flags = 0;
  if (fineMaskProxy != nullptr) {
    flags |= 1;
  }
  if (coarseMaskProxy != nullptr) {
    flags |= 2;
  }
  if (params.dispersion >= 0.01f) {
    flags |= 4;
  }
  if (params.lightIntensity > 0.0f) {
    flags |= 8;
  }
  bytesKey->write(flags);
}

size_t GlassRefractionFragmentProcessor::onCountTextureSamplers() const {
  size_t count = 1;
  if (fineMaskProxy != nullptr) {
    count++;
  }
  if (coarseMaskProxy != nullptr) {
    count++;
  }
  return count;
}

std::shared_ptr<Texture> GlassRefractionFragmentProcessor::onTextureAt(size_t index) const {
  const auto& proxy = (index == 0) ? sourceProxy : (index == 1) ? fineMaskProxy : coarseMaskProxy;
  if (proxy == nullptr) {
    return nullptr;
  }
  auto textureView = proxy->getTextureView();
  return textureView == nullptr ? nullptr : textureView->getTexture();
}

SamplerState GlassRefractionFragmentProcessor::onSamplerStateAt(size_t index) const {
  if (index == 0) {
    return SamplerState(TileMode::Clamp, TileMode::Clamp);
  }
  return SamplerState(TileMode::Decal, TileMode::Decal);
}

}  // namespace tgfx
