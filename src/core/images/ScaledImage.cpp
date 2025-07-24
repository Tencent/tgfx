/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
std::shared_ptr<Image> ScaledImage::MakeFrom(std::shared_ptr<Image> image, const ISize& size,
                                             const SamplingOptions& sampling) {
  if (image == nullptr || size.width <= 0 || size.height <= 0) {
    return nullptr;
  }
  if (image->width() == size.width && image->height() == size.height) {
    return image;
  }
  auto scaledImage =
      std::make_shared<ScaledImage>(std::move(image), size.width, size.height, sampling);
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

ScaledImage::ScaledImage(std::shared_ptr<Image> image, int width, int height,
                         const SamplingOptions& sampling)
    : source(std::move(image)), _width(width), _height(height), sampling(sampling) {
}

std::shared_ptr<Image> ScaledImage::onMakeScaled(const ISize& size,
                                                 const SamplingOptions& sampling) const {
  return MakeFrom(source, size, sampling);
}

std::shared_ptr<Image> ScaledImage::onMakeMipmapped(bool enabled) const {
  if (enabled == source->hasMipmaps()) {
    return weakThis.lock();
  }
  auto newSource = source->onMakeMipmapped(enabled);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<ScaledImage>(
      new ScaledImage(std::move(newSource), _width, _height, sampling));
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<Image> ScaledImage::onMakeDecoded(Context* context, bool) const {
  // There is no need to pass tryHardware (disabled) to the source image, as our texture proxy is
  // not locked from the source image.
  auto newSource = source->onMakeDecoded(context);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<ScaledImage>(
      new ScaledImage(std::move(newSource), _width, _height, sampling));
  newImage->weakThis = newImage;
  return newImage;
}

PlacementPtr<FragmentProcessor> ScaledImage::asFragmentProcessor(const FPArgs& args,
                                                                 const SamplingArgs& samplingArgs,
                                                                 const Matrix* uvMatrix) const {
  auto drawBounds = args.drawRect;
  if (uvMatrix) {
    drawBounds = uvMatrix->mapRect(drawBounds);
  }
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto rect = Rect::MakeWH(_width, _height);
  if (!rect.intersect(drawBounds)) {
    return nullptr;
  }
  rect.roundOut();
  auto mipmapped = samplingArgs.sampling.mipmapMode != MipmapMode::None && hasMipmaps();
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(rect.width()), static_cast<int>(rect.height()), isAlphaOnly(),
      1, mipmapped, ImageOrigin::TopLeft, BackingFit::Approx);

  if (renderTarget == nullptr) {
    return nullptr;
  }

  auto uvScaleX = static_cast<float>(sourceWidth) / static_cast<float>(_width);
  auto uvScaleY = static_cast<float>(sourceHeight) / static_cast<float>(_height);
  Matrix sourceUVMatrix = Matrix::MakeScale(uvScaleX, uvScaleY);
  sourceUVMatrix.preTranslate(rect.left, rect.top);
  auto drawRect = Rect::MakeWH(rect.width(), rect.height());
  FPArgs fpArgs(args.context, args.renderFlags, drawRect);
  auto processor =
      FragmentProcessor::Make(source, fpArgs, sampling, SrcRectConstraint::Fast, &sourceUVMatrix);
  if (processor == nullptr) {
    return nullptr;
  }
  auto drawingManager = renderTarget->getContext()->drawingManager();
  drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags);

  auto finalUVMatrix = Matrix::MakeTrans(-rect.left, -rect.top);
  if (uvMatrix) {
    finalUVMatrix.preConcat(*uvMatrix);
  }
  auto newSamplingArgs = samplingArgs;
  if (samplingArgs.sampleArea) {
    newSamplingArgs.sampleArea = samplingArgs.sampleArea->makeOffset(-rect.left, -rect.top);
  }
  return TiledTextureEffect::Make(renderTarget->asTextureProxy(), newSamplingArgs, &finalUVMatrix,
                                  isAlphaOnly());
}

std::shared_ptr<TextureProxy> ScaledImage::lockTextureProxy(const TPArgs& args) const {
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, width(), height(), isAlphaOnly() && alphaRenderable, 1, args.mipmapped,
      ImageOrigin::TopLeft, BackingFit::Approx);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto uvScaleX = static_cast<float>(sourceWidth) / static_cast<float>(_width);
  auto uvScaleY = static_cast<float>(sourceHeight) / static_cast<float>(_height);
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
