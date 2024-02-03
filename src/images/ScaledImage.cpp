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

#include "ScaledImage.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
std::shared_ptr<Image> ScaledImage::MakeFrom(std::shared_ptr<Image> source,
                                             float rasterizationScale, bool mipMapped) {
  if (source == nullptr || rasterizationScale <= 0) {
    return nullptr;
  }
  auto scaledImage = std::shared_ptr<ScaledImage>(
      new ScaledImage(ResourceKey::NewWeak(), std::move(source), rasterizationScale, mipMapped));
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

ScaledImage::ScaledImage(ResourceKey resourceKey, std::shared_ptr<Image> source,
                         float rasterizationScale, bool mipMapped)
    : RasterImage(std::move(resourceKey)), source(std::move(source)),
      rasterizationScale(rasterizationScale), mipMapped(mipMapped) {
}

static int GetScaledSize(int size, float scale) {
  return static_cast<int>(ceilf(static_cast<float>(size) * scale));
}

int ScaledImage::width() const {
  return GetScaledSize(source->width(), rasterizationScale);
}

int ScaledImage::height() const {
  return GetScaledSize(source->height(), rasterizationScale);
}

std::shared_ptr<Image> ScaledImage::makeRasterized(float scaleFactor) const {
  if (scaleFactor == 1.0f) {
    return weakThis.lock();
  }
  return MakeFrom(source, rasterizationScale * scaleFactor);
}

std::shared_ptr<Image> ScaledImage::onMakeDecoded(Context* context) const {
  auto newSource = source->makeDecoded(context);
  if (newSource == source) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<ScaledImage>(
      new ScaledImage(resourceKey, std::move(newSource), rasterizationScale, mipMapped));
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<Image> ScaledImage::onMakeMipMapped() const {
  return ScaledImage::MakeFrom(source, rasterizationScale, true);
}

std::shared_ptr<TextureProxy> ScaledImage::onLockTextureProxy(Context* context,
                                                              uint32_t renderFlags) const {
  auto proxyProvider = context->proxyProvider();
  auto hasCache = proxyProvider->hasResourceProxy(resourceKey);
  auto format = isAlphaOnly() ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  auto textureProxy = proxyProvider->createTextureProxy(
      resourceKey, width(), height(), format, mipMapped, ImageOrigin::TopLeft, renderFlags);
  if (hasCache) {
    return textureProxy;
  }
  auto mipMapMode = source->hasMipmaps() ? MipMapMode::Linear : MipMapMode::None;
  SamplingOptions sampling(FilterMode::Linear, mipMapMode);
  auto sourceFlags = renderFlags | RenderFlags::DisableCache;
  ImageFPArgs imageArgs(context, sampling, sourceFlags);
  auto processor = FragmentProcessor::MakeFromImage(source, imageArgs);
  if (processor == nullptr) {
    return nullptr;
  }
  auto renderTarget = proxyProvider->createRenderTargetProxy(textureProxy, format);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  RenderContext renderContext(renderTarget);
  auto localMatrix = Matrix::MakeScale(1.0f / rasterizationScale);
  renderContext.fillWithFP(std::move(processor), localMatrix, true);
  return textureProxy;
}

}  // namespace tgfx
