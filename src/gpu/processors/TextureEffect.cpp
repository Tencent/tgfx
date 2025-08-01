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

#include "TextureEffect.h"
#include "core/utils/Log.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> TextureEffect::Make(std::shared_ptr<TextureProxy> proxy,
                                                    const SamplingArgs& args,
                                                    const Matrix* uvMatrix, bool forceAsMask) {
  if (proxy == nullptr) {
    return nullptr;
  }
  auto isAlphaOnly = proxy->isAlphaOnly();
  auto processor = MakeRGBAAA(proxy, args, {}, uvMatrix);
  if (forceAsMask && !isAlphaOnly) {
    auto drawingBuffer = proxy->getContext()->drawingBuffer();
    processor = FragmentProcessor::MulInputByChildAlpha(drawingBuffer, std::move(processor));
  }
  return processor;
}

TextureEffect::TextureEffect(std::shared_ptr<TextureProxy> proxy, const SamplingOptions& sampling,
                             SrcRectConstraint constraint, const Point& alphaStart,
                             const Matrix& uvMatrix, const std::optional<Rect>& subset)
    : FragmentProcessor(ClassID()), textureProxy(std::move(proxy)), samplerState(sampling),
      constraint(constraint), alphaStart(alphaStart),
      coordTransform(uvMatrix, textureProxy.get(), alphaStart), subset(subset) {
  addCoordTransform(&coordTransform);
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
    flags |= yuvTexture->yuvFormat() == YUVFormat::I420 ? 0 : 4;
    flags |= IsLimitedYUVColorRange(yuvTexture->colorSpace()) ? 0 : 8;
  }
  flags |= needSubset() ? 16 : 0;
  flags |= constraint == SrcRectConstraint::Strict ? 32 : 0;
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

bool TextureEffect::needSubset() const {
  auto bounds = Rect::MakeWH(textureProxy->width(), textureProxy->height());
  if (subset.has_value() && !(*subset).contains(bounds)) {
    // if subset equal to bounds, we don't need to use subset.
    return true;
  }
  auto texture = getTexture();
  if (textureProxy->width() != texture->width() || textureProxy->height() != texture->height()) {
    // If the texture size is different from the proxy size, we need to use subset.
    return true;
  }
  return false;
}

}  // namespace tgfx
