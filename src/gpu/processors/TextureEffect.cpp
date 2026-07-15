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
#include <algorithm>
#include "core/utils/Log.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> TextureEffect::Make(BlockAllocator* allocator,
                                                    std::shared_ptr<TextureProxy> proxy,
                                                    const SamplingArgs& args,
                                                    const Matrix* uvMatrix, bool forceAsMask) {
  if (proxy == nullptr) {
    return nullptr;
  }
  auto isAlphaOnly = proxy->isAlphaOnly();
  auto processor = MakeRGBAAA(allocator, proxy, args, {}, uvMatrix);
  if (forceAsMask && !isAlphaOnly) {
    processor = FragmentProcessor::MulInputByChildAlpha(allocator, std::move(processor));
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
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return;
  }
  uint32_t flags = alphaStart == Point::Zero() ? 1 : 0;
  // Sometimes textureProxy->isAlphaOnly() != texture->isAlphaOnly(), we use
  // textureProxy->isAlphaOnly() to determine the alpha-only flag.
  flags |= textureProxy->isAlphaOnly() ? 2 : 0;
  auto yuvTexture = getYUVTexture();
  if (yuvTexture) {
    flags |= yuvTexture->yuvFormat() == YUVFormat::I420 ? 0 : 4;
    flags |= IsLimitedYUVColorRange(yuvTexture->yuvColorSpace()) ? 0 : 8;
  }
  flags |= needSubset() ? 16 : 0;
  flags |= constraint == SrcRectConstraint::Strict ? 32 : 0;
  flags |= coordTransform.matrix.hasPerspective() ? 64 : 0;
  bytesKey->write(flags);
}

size_t TextureEffect::onCountTextureSamplers() const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return 0;
  }
  if (textureView->isYUV()) {
    return reinterpret_cast<YUVTextureView*>(textureView)->textureCount();
  }
  return 1;
}

std::shared_ptr<Texture> TextureEffect::onTextureAt(size_t index) const {
  auto textureView = getTextureView();
  if (textureView == nullptr) {
    return nullptr;
  }
  if (textureView->isYUV()) {
    return reinterpret_cast<YUVTextureView*>(textureView)->getTextureAt(index);
  }
  return textureView->getTexture();
}

TextureView* TextureEffect::getTextureView() const {
  return textureProxy->getTextureView().get();
}

YUVTextureView* TextureEffect::getYUVTexture() const {
  auto textureView = textureProxy->getTextureView().get();
  if (textureView && textureView->isYUV()) {
    return reinterpret_cast<YUVTextureView*>(textureView);
  }
  return nullptr;
}

bool TextureEffect::isYUV() const {
  return getYUVTexture() != nullptr;
}

bool TextureEffect::isAlphaOnly() const {
  return textureProxy->isAlphaOnly();
}

bool TextureEffect::needSubset() const {
  auto bounds = Rect::MakeWH(textureProxy->width(), textureProxy->height());
  if (subset.has_value() && !(*subset).contains(bounds)) {
    // if subset equal to bounds, we don't need to use subset.
    return true;
  }
  auto textureView = getTextureView();
  if (textureProxy->width() != textureView->width() ||
      textureProxy->height() != textureView->height()) {
    // If the texture size is different from the proxy size, we need to use subset.
    return true;
  }
  return false;
}

void TextureEffect::computeSubsetRect(float rect[4]) const {
  auto textureView = getTextureView();
  auto subsetRect = subset.value_or(Rect::MakeWH(textureProxy->width(), textureProxy->height()));
  if (needSubset()) {
    if (samplerState.magFilterMode == samplerState.minFilterMode &&
        samplerState.magFilterMode == FilterMode::Nearest) {
      subsetRect.roundOut();
    }
    auto type = textureView->getTexture()->type();
    // Normally this would just need to take 1/2 a texel off each end, but because the chroma
    // channels of YUV420 images are subsampled we may need to shrink the crop region by a whole
    // texel on each side for external textures.
    auto inset = type == TextureType::External ? 1.0f : 0.5f;
    subsetRect = subsetRect.makeInset(inset, inset);
  }
  rect[0] = subsetRect.left;
  rect[1] = subsetRect.top;
  rect[2] = subsetRect.right;
  rect[3] = subsetRect.bottom;
  if (textureView->origin() == ImageOrigin::BottomLeft) {
    auto h = static_cast<float>(textureView->height());
    rect[1] = h - rect[1];
    rect[3] = h - rect[3];
    std::swap(rect[1], rect[3]);
  }
  auto type = textureView->getTexture()->type();
  if (type != TextureType::Rectangle) {
    auto lt = textureView->getTextureCoord(rect[0], rect[1]);
    auto rb = textureView->getTextureCoord(rect[2], rect[3]);
    rect[0] = lt.x;
    rect[1] = lt.y;
    rect[2] = rb.x;
    rect[3] = rb.y;
  }
}

}  // namespace tgfx
