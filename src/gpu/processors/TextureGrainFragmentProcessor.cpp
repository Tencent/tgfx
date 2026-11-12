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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/processors/TextureGrainFragmentProcessor.h"

namespace tgfx {

TextureGrainFragmentProcessor::TextureGrainFragmentProcessor(
    std::shared_ptr<TextureProxy> source, std::shared_ptr<TextureView> gradientView,
    const TextureGrainParams& params, const SamplingOptions& sampling, bool useHardwareDecal,
    const Matrix& coordMatrix)
    : FragmentProcessor(ClassID()), sourceProxy(std::move(source)),
      gradientView(std::move(gradientView)), params(params), sampling(sampling),
      useHardwareDecal(useHardwareDecal), coordTransform(coordMatrix) {
  addCoordTransform(&coordTransform);
}

void TextureGrainFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = 0;
  if (params.clipToShape) {
    flags |= 1;
  }
  if (coordTransform.matrix.hasPerspective()) {
    flags |= 2;
  }
  if (useHardwareDecal) {
    flags |= 4;
  }
  if (sourceProxy->isAlphaOnly()) {
    flags |= 8;
  }
  auto yuvTexture = getYUVTexture();
  if (yuvTexture != nullptr) {
    flags |= 16;
    if (yuvTexture->yuvFormat() == YUVFormat::NV12) {
      flags |= 32;
    }
    if (IsLimitedYUVColorRange(yuvTexture->yuvColorSpace())) {
      flags |= 64;
    }
  }
  bytesKey->write(flags);
}

size_t TextureGrainFragmentProcessor::onCountTextureSamplers() const {
  return sourceSamplerCount() + 1;
}

std::shared_ptr<Texture> TextureGrainFragmentProcessor::onTextureAt(size_t index) const {
  auto textureView = getTextureView();
  if (index < sourceSamplerCount()) {
    if (textureView->isYUV()) {
      return reinterpret_cast<YUVTextureView*>(textureView)->getTextureAt(index);
    }
    return textureView->getTexture();
  }
  return gradientView == nullptr ? nullptr : gradientView->getTexture();
}

SamplerState TextureGrainFragmentProcessor::onSamplerStateAt(size_t index) const {
  if (index < sourceSamplerCount()) {
    auto tileMode = useHardwareDecal ? TileMode::Decal : TileMode::Clamp;
    return SamplerState(tileMode, tileMode, sampling);
  }
  return SamplerState(TileMode::Repeat, TileMode::Repeat, FilterMode::Nearest);
}

TextureView* TextureGrainFragmentProcessor::getTextureView() const {
  return sourceProxy->getTextureView().get();
}

YUVTextureView* TextureGrainFragmentProcessor::getYUVTexture() const {
  auto textureView = getTextureView();
  if (textureView != nullptr && textureView->isYUV()) {
    return reinterpret_cast<YUVTextureView*>(textureView);
  }
  return nullptr;
}

size_t TextureGrainFragmentProcessor::sourceSamplerCount() const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return 0;
  }
  if (textureView->isYUV()) {
    return reinterpret_cast<YUVTextureView*>(textureView)->textureCount();
  }
  return 1;
}

}  // namespace tgfx
