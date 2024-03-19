/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TextureEffect.h"
#include "gpu/ProxyProvider.h"
#include "utils/Log.h"

namespace tgfx {
std::unique_ptr<FragmentProcessor> TextureEffect::Make(std::shared_ptr<TextureProxy> proxy,
                                                       const SamplingOptions& sampling,
                                                       const Matrix* localMatrix) {
  return MakeRGBAAA(std::move(proxy), Point::Zero(), sampling, localMatrix);
}

TextureEffect::TextureEffect(std::shared_ptr<TextureProxy> proxy, SamplingOptions sampling,
                             const Point& alphaStart, const Matrix& localMatrix)
    : FragmentProcessor(ClassID()), textureProxy(std::move(proxy)), samplerState(sampling),
      alphaStart(alphaStart), coordTransform(localMatrix, textureProxy.get(), alphaStart) {
  addCoordTransform(&coordTransform);
}

bool TextureEffect::onIsEqual(const FragmentProcessor& processor) const {
  const auto& that = static_cast<const TextureEffect&>(processor);
  return textureProxy == that.textureProxy && alphaStart == that.alphaStart &&
         coordTransform.matrix == that.coordTransform.matrix && samplerState == that.samplerState;
}

void TextureEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  auto texture = getTexture();
  if (texture == nullptr) {
    return;
  }
  uint32_t flags = alphaStart == Point::Zero() ? 1 : 0;
  // Sometimes textureProxy->isAlphaOnly() != texture->isAlphaOnly(), we use
  // textureProxy->isAlphaOnly() to determine the alpha-only flag.
  flags |= textureProxy->isAlphaOnly() ? 2 : 0;
  auto yuvTexture = getYUVTexture();
  if (yuvTexture) {
    flags |= yuvTexture->pixelFormat() == YUVPixelFormat::I420 ? 0 : 4;
    flags |= IsLimitedYUVColorRange(yuvTexture->colorSpace()) ? 0 : 8;
  }
  bytesKey->write(flags);
}

size_t TextureEffect::onCountTextureSamplers() const {
  auto texture = getTexture();
  if (texture == nullptr) {
    return 0;
  }
  if (texture->isYUV()) {
    return reinterpret_cast<YUVTexture*>(texture)->samplerCount();
  }
  return 1;
}

const TextureSampler* TextureEffect::onTextureSampler(size_t index) const {
  auto texture = getTexture();
  if (texture == nullptr) {
    return nullptr;
  }
  if (texture->isYUV()) {
    return reinterpret_cast<YUVTexture*>(texture)->getSamplerAt(index);
  }
  return texture->getSampler();
}

Texture* TextureEffect::getTexture() const {
  return textureProxy->getTexture().get();
}

YUVTexture* TextureEffect::getYUVTexture() const {
  auto texture = textureProxy->getTexture().get();
  if (texture && texture->isYUV()) {
    return reinterpret_cast<YUVTexture*>(texture);
  }
  return nullptr;
}
}  // namespace tgfx
