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
                                             float rasterizationScale, SamplingOptions sampling) {
  if (source == nullptr || rasterizationScale <= 0) {
    return nullptr;
  }
  auto hasMipmap = source->hasMipmaps();
  auto needMipmap = sampling.mipmapMode != MipmapMode::None;
  if (hasMipmap != needMipmap) {
    auto newSource = source->makeMipmapped(needMipmap);
    if (newSource != nullptr) {
      source = std::move(newSource);
    }
  }
  auto scaledImage = std::shared_ptr<ScaledImage>(
      new ScaledImage(ResourceKey::NewWeak(), std::move(source), rasterizationScale, sampling));
  scaledImage->weakThis = scaledImage;
  return hasMipmap ? scaledImage->makeMipmapped(true) : scaledImage;
}

ScaledImage::ScaledImage(ResourceKey resourceKey, std::shared_ptr<Image> source,
                         float rasterizationScale, SamplingOptions sampling)
    : RasterImage(std::move(resourceKey)), source(std::move(source)),
      rasterizationScale(rasterizationScale), sampling(sampling) {
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

std::shared_ptr<Image> ScaledImage::makeRasterized(float scaleFactor,
                                                   SamplingOptions options) const {
  if (scaleFactor == 1.0f) {
    return weakThis.lock();
  }
  return ScaledImage::MakeFrom(source, rasterizationScale * scaleFactor, options);
}

std::shared_ptr<Image> ScaledImage::onMakeDecoded(Context* context, bool) const {
  // There is no need to pass tryHardware to the source image, as our texture proxy is not locked
  // from the source image.
  auto newSource = source->onMakeDecoded(context);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<ScaledImage>(
      new ScaledImage(resourceKey, std::move(newSource), rasterizationScale, sampling));
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<TextureProxy> ScaledImage::onLockTextureProxy(Context* context,
                                                              const ResourceKey& key,
                                                              bool mipmapped,
                                                              uint32_t renderFlags) const {
  auto proxyProvider = context->proxyProvider();
  auto hasCache = proxyProvider->hasResourceProxy(key);
  auto format = isAlphaOnly() ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  auto textureProxy = proxyProvider->createTextureProxy(key, width(), height(), format, mipmapped,
                                                        ImageOrigin::TopLeft, renderFlags);
  if (hasCache) {
    return textureProxy;
  }
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
