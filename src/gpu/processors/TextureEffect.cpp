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
                                                    const Matrix* uvMatrix, bool forceAsMask,
                                                    std::shared_ptr<ColorSpace> dstColorSpace) {
  if (proxy == nullptr) {
    return nullptr;
  }
  auto isAlphaOnly = proxy->isAlphaOnly();
  auto processor = MakeRGBAAA(proxy, args, {}, uvMatrix, std::move(dstColorSpace));
  if (forceAsMask && !isAlphaOnly) {
    auto drawingBuffer = proxy->getContext()->drawingBuffer();
    processor = FragmentProcessor::MulInputByChildAlpha(drawingBuffer, std::move(processor));
  }
  return processor;
}

TextureEffect::TextureEffect(std::shared_ptr<TextureProxy> proxy, const SamplingOptions& sampling,
                             SrcRectConstraint constraint, const Point& alphaStart,
                             const Matrix& uvMatrix, const std::optional<Rect>& subset,
                             std::shared_ptr<ColorSpace> dstColorSpace)
    : FragmentProcessor(ClassID()), textureProxy(std::move(proxy)), samplerState(sampling),
      constraint(constraint), alphaStart(alphaStart),
      coordTransform(uvMatrix, textureProxy.get(), alphaStart), subset(subset),
      dstColorSpace(std::move(dstColorSpace)) {
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
    flags |= IsLimitedYUVColorRange(yuvTexture->colorSpace()) ? 0 : 8;
  }
  flags |= needSubset() ? 16 : 0;
  flags |= constraint == SrcRectConstraint::Strict ? 32 : 0;
  bytesKey->write(flags);
  auto srcColorSpace = getTextureView()->gamutColorSpace();
  auto steps = std::make_shared<ColorSpaceXformSteps>(
      srcColorSpace.get(), AlphaType::Premultiplied, dstColorSpace.get(), AlphaType::Premultiplied);
  uint64_t xformKey = ColorSpaceXformSteps::XFormKey(steps.get());
  auto key = reinterpret_cast<uint32_t*>(&xformKey);
  bytesKey->write(key[0]);
  bytesKey->write(key[1]);
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

std::shared_ptr<GPUTexture> TextureEffect::onTextureAt(size_t index) const {
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

}  // namespace tgfx
