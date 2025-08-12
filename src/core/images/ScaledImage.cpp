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

ScaledImage::ScaledImage(std::shared_ptr<Image> image, int width, int height,
                         const SamplingOptions& sampling, bool mipmapped)
    : TransformImage(std::move(image)), _width(width), _height(height), sampling(sampling),
      mipmapped(mipmapped) {
}

PlacementPtr<FragmentProcessor> ScaledImage::asFragmentProcessor(const FPArgs& args,
                                                                 const SamplingArgs& samplingArgs,
                                                                 const Matrix* uvMatrix) const {
  auto drawBounds = args.drawRect;
  if (uvMatrix) {
    drawBounds = uvMatrix->mapRect(drawBounds);
  }
  auto drawRect = Rect::MakeWH(_width, _height);
  if (!drawRect.intersect(drawBounds)) {
    return nullptr;
  }
  drawRect.roundOut();
  auto mipmapped = hasMipmaps() && samplingArgs.sampling.mipmapMode != MipmapMode::None;
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped, args.drawScale);
  auto textureProxy = lockTextureProxy(tpArgs, drawRect);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto extraMatrix = Matrix::MakeTrans(-drawRect.left, -drawRect.top);
  extraMatrix.preScale(drawRect.width() / static_cast<float>(textureProxy->width()),
                       drawRect.height() / static_cast<float>(textureProxy->height()));

  auto fpMatrix = extraMatrix;
  if (uvMatrix) {
    fpMatrix.preConcat(*uvMatrix);
  }
  auto newSamplingArgs = samplingArgs;
  if (samplingArgs.sampleArea) {
    newSamplingArgs.sampleArea = extraMatrix.mapRect(*samplingArgs.sampleArea);
  }
  return TiledTextureEffect::Make(textureProxy, newSamplingArgs, &fpMatrix, isAlphaOnly());
}

std::shared_ptr<TextureProxy> ScaledImage::lockTextureProxy(const TPArgs& args) const {
  return lockTextureProxy(args, Rect::MakeWH(width(), height()));
}

std::shared_ptr<TextureProxy> ScaledImage::lockTextureProxy(const TPArgs& args,
                                                            const Rect& drawRect) const {
  if (args.drawScale > 1.f) {
    return nullptr;
  }
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto scaledRect = drawRect;
  if (args.drawScale < 1.f) {
    scaledRect.scale(args.drawScale, args.drawScale);
  }
  scaledRect.roundOut();
  auto drawScaleX = scaledRect.width() / drawRect.width();
  auto drawScaleY = scaledRect.height() / drawRect.height();
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(scaledRect.width()), static_cast<int>(scaledRect.height()),
      alphaRenderable && isAlphaOnly(), 1, hasMipmaps() && args.mipmapped, ImageOrigin::TopLeft,
      args.backingFit);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  Point scales =
      Point::Make(drawScaleX * static_cast<float>(_width) / static_cast<float>(source->width()),
                  drawScaleY * static_cast<float>(_height) / static_cast<float>(source->height()));
  Matrix sourceUVMatrix = Matrix::MakeScale(1.0f / scales.x, 1.0f / scales.y);
  sourceUVMatrix.preTranslate(scaledRect.left, scaledRect.top);
  FPArgs fpArgs(args.context, args.renderFlags,
                Rect::MakeWH(renderTarget->width(), renderTarget->height()),
                std::max(scales.x, scales.y));
  auto processor =
      FragmentProcessor::Make(source, fpArgs, sampling, SrcRectConstraint::Fast, &sourceUVMatrix);
  if (processor == nullptr) {
    return nullptr;
  }
  auto drawingManager = renderTarget->getContext()->drawingManager();
  drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags);
  return renderTarget->asTextureProxy();
}

std::shared_ptr<Image> ScaledImage::onMakeScaled(int newWidth, int newHeight,
                                                 const SamplingOptions& sampling) const {
  if (newWidth == source->width() && newHeight == source->height()) {
    return source;
  }
  auto result = source->makeScaled(newWidth, newHeight, sampling);
  return result->makeMipmapped(hasMipmaps());
}

std::shared_ptr<Image> ScaledImage::onMakeMipmapped(bool enabled) const {
  auto scaledImage = std::make_shared<ScaledImage>(source, _width, _height, sampling, enabled);
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

std::shared_ptr<Image> ScaledImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  auto scaledImage = std::make_shared<ScaledImage>(newSource, _width, _height, sampling, mipmapped);
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

}  // namespace tgfx
