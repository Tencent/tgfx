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

#include "RasterImage.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
std::shared_ptr<RasterImage> RasterImage::MakeFrom(std::shared_ptr<Image> source,
                                                   float rasterizationScale,
                                                   const SamplingOptions& sampling) {
  if (source == nullptr || rasterizationScale <= 0) {
    return nullptr;
  }
  auto hasMipmap = source->hasMipmaps();
  auto needMipmap = sampling.mipmapMode != MipmapMode::None;
  // The source image's mipmap state is controlled by sampling options, while the caller defines the
  // mipmap state of the returned raster image.
  if (hasMipmap != needMipmap) {
    auto newSource = source->makeMipmapped(needMipmap);
    if (newSource != nullptr) {
      source = std::move(newSource);
    }
  }
  auto rasterImage = std::shared_ptr<RasterImage>(
      new RasterImage(UniqueKey::Make(), std::move(source), rasterizationScale, sampling));
  rasterImage->weakThis = rasterImage;
  return rasterImage;
}

RasterImage::RasterImage(UniqueKey uniqueKey, std::shared_ptr<Image> source,
                         float rasterizationScale, const SamplingOptions& sampling)
    : ResourceImage(std::move(uniqueKey)), source(std::move(source)),
      rasterizationScale(rasterizationScale), sampling(sampling) {
}

static int GetScaledSize(int size, float scale) {
  return static_cast<int>(ceilf(static_cast<float>(size) * scale));
}

int RasterImage::width() const {
  return GetScaledSize(source->width(), rasterizationScale);
}

int RasterImage::height() const {
  return GetScaledSize(source->height(), rasterizationScale);
}

std::shared_ptr<Image> RasterImage::makeRasterized(float scaleFactor,
                                                   const SamplingOptions& options) const {
  if (scaleFactor == 1.0f) {
    return weakThis.lock();
  }
  return RasterImage::MakeFrom(source, rasterizationScale * scaleFactor, options);
}

std::shared_ptr<Image> RasterImage::onMakeDecoded(Context* context, bool) const {
  // There is no need to pass tryHardware to the source image, as our texture proxy is not locked
  // from the source image.
  auto newSource = source->onMakeDecoded(context);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<RasterImage>(
      new RasterImage(uniqueKey, std::move(newSource), rasterizationScale, sampling));
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<TextureProxy> RasterImage::onLockTextureProxy(Context* context,
                                                              const UniqueKey& key, bool mipmapped,
                                                              uint32_t renderFlags) const {
  auto proxyProvider = context->proxyProvider();
  auto textureProxy = std::static_pointer_cast<TextureProxy>(proxyProvider->findProxy(key));
  if (textureProxy != nullptr) {
    return textureProxy;
  }
  auto hasResourceCache = context->resourceCache()->hasUniqueResource(key);
  auto alphaRenderable = context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format = isAlphaOnly() && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  textureProxy = proxyProvider->createTextureProxy(key, width(), height(), format, mipmapped,
                                                   ImageOrigin::TopLeft, renderFlags);
  if (hasResourceCache) {
    return textureProxy;
  }
  auto renderTarget = proxyProvider->createRenderTargetProxy(textureProxy, format);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto sourceFlags = renderFlags | RenderFlags::DisableCache;
  auto drawRect = Rect::MakeWH(width(), height());
  FPArgs args(context, sourceFlags, drawRect, Matrix::I());
  auto localMatrix = Matrix::MakeScale(1.0f / rasterizationScale);
  auto processor = FragmentProcessor::Make(source, args, sampling, &localMatrix);
  if (processor == nullptr) {
    return nullptr;
  }
  RenderContext renderContext(renderTarget);
  renderContext.fillWithFP(std::move(processor), Matrix::I(), true);
  return textureProxy;
}

}  // namespace tgfx
