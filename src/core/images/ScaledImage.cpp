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
std::shared_ptr<Image> ScaledImage::MakeFrom(std::shared_ptr<Image> image, int width, int height,
                                             const SamplingOptions& sampling) {
  DEBUG_ASSERT(width > 0 && height > 0 && image != nullptr);
  if (image->width() == width && image->height() == height) {
    return image;
  }
  auto scaledImage = std::make_shared<ScaledImage>(std::move(image), width, height, sampling);
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

ScaledImage::ScaledImage(std::shared_ptr<Image> image, int width, int height,
                         const SamplingOptions& sampling)
    : TransformImage(std::move(image)), _width(width), _height(height), sampling(sampling) {
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
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped);
  auto textureProxy = lockTextureProxy(tpArgs, drawRect);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto finalUVMatrix = Matrix::MakeTrans(-drawRect.left, -drawRect.top);
  if (uvMatrix) {
    finalUVMatrix.preConcat(*uvMatrix);
  }
  auto newSamplingArgs = samplingArgs;
  if (samplingArgs.sampleArea) {
    newSamplingArgs.sampleArea = samplingArgs.sampleArea->makeOffset(-drawRect.left, -drawRect.top);
  }
  return TiledTextureEffect::Make(textureProxy, newSamplingArgs, &finalUVMatrix, isAlphaOnly());
}

std::shared_ptr<TextureProxy> ScaledImage::lockTextureProxy(const TPArgs& args) const {
  return lockTextureProxy(args, Rect::MakeWH(width(), height()));
}

std::shared_ptr<TextureProxy> ScaledImage::lockTextureProxy(const TPArgs& args,
                                                            const Rect& drawRect) const {
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(drawRect.width()), static_cast<int>(drawRect.height()),
      alphaRenderable && isAlphaOnly(), 1, args.mipmapped, ImageOrigin::TopLeft,
      BackingFit::Approx);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto uvScaleX = static_cast<float>(source->width()) / static_cast<float>(_width);
  auto uvScaleY = static_cast<float>(source->height()) / static_cast<float>(_height);
  Matrix sourceUVMatrix = Matrix::MakeScale(uvScaleX, uvScaleY);
  sourceUVMatrix.preTranslate(drawRect.left, drawRect.top);
  FPArgs fpArgs(args.context, args.renderFlags, Rect::MakeWH(drawRect.width(), drawRect.height()));
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
  return MakeFrom(source, newWidth, newHeight, sampling);
}

std::shared_ptr<Image> ScaledImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  auto scaledImage = std::make_shared<ScaledImage>(newSource, _width, _height, sampling);
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

}  // namespace tgfx
