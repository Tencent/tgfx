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

#include "RasterizedImage.h"
#include "core/images/SubsetImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/DrawOp.h"

namespace tgfx {
std::shared_ptr<Image> RasterizedImage::MakeFrom(std::shared_ptr<Image> source,
                                                 int newWeight, int newHeight,
                                                 const SamplingOptions& sampling) {
  if (source == nullptr || newWeight <= 0 || newHeight <= 0) {
    return nullptr;
  }
  auto result = std::shared_ptr<RasterizedImage>(
      new RasterizedImage(UniqueKey::Make(), std::move(source), newWeight, newHeight, sampling));
  result->weakThis = result;
  return result;
}

RasterizedImage::RasterizedImage(UniqueKey uniqueKey, std::shared_ptr<Image> source,
                                 int width, int height, const SamplingOptions& sampling)
    : ResourceImage(std::move(uniqueKey)), source(std::move(source)),
      _width(width), _height(height), sampling(sampling) {
}

int RasterizedImage::width() const {
  return _width;
}

int RasterizedImage::height() const {
  return _height;
}

std::shared_ptr<Image> RasterizedImage::makeRasterized() const {
  return MakeFrom(source, width(), height(), {});
}

std::shared_ptr<Image> RasterizedImage::makeScaled(int newWidth, int newHeight,
                                                   const SamplingOptions& sampling) const {
  return MakeFrom(source, newWidth, newHeight, sampling);
}

std::shared_ptr<Image> RasterizedImage::onMakeDecoded(Context* context, bool) const {
  // There is no need to pass tryHardware (disabled) to the source image, as our texture proxy is
  // not locked from the source image.
  auto newSource = source->onMakeDecoded(context);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<RasterizedImage>(
      new RasterizedImage(uniqueKey, std::move(newSource), width(), height(), sampling));
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<TextureProxy> RasterizedImage::onLockTextureProxy(const TPArgs& args,
                                                                  const UniqueKey& key) const {
  auto proxyProvider = args.context->proxyProvider();
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(key);
  if (textureProxy != nullptr) {
    return textureProxy;
  }
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format = isAlphaOnly() && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  auto renderTarget = proxyProvider->createRenderTargetProxy(key, width(), height(), format, 1,
                                                             args.mipmapped, ImageOrigin::TopLeft,
                                                             BackingFit::Exact, args.renderFlags);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto scaledWidth = width();
  auto scaledHeight = height();
  auto uvScaleX = static_cast<float>(sourceWidth) / static_cast<float>(scaledWidth);
  auto uvScaleY = static_cast<float>(sourceHeight) / static_cast<float>(scaledHeight);
  Matrix uvMatrix = Matrix::MakeScale(uvScaleX, uvScaleY);
  auto drawRect = Rect::MakeWH(width(), height());
  FPArgs fpArgs(args.context, args.renderFlags, drawRect);
  auto processor =
      FragmentProcessor::Make(source, fpArgs, sampling, SrcRectConstraint::Fast, &uvMatrix);
  if (processor == nullptr) {
    return nullptr;
  }
  auto drawingManager = renderTarget->getContext()->drawingManager();
  drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags);
  return renderTarget->asTextureProxy();
}
}  // namespace tgfx
