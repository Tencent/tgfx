/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GaussianBlurImageFilter.h"
#include <memory>
#include <utility>
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

#define MAX_BLUR_SIGMA 3.0f

std::shared_ptr<ImageFilter> ImageFilter::Blur(float blurrinessX, float blurrinessY,
                                               TileMode tileMode) {
  if (blurrinessX < 0 || blurrinessY < 0 || (blurrinessX == 0 && blurrinessY == 0)) {
    return nullptr;
  }
  return std::make_shared<GaussianBlurImageFilter>(blurrinessX, blurrinessY, tileMode);
}

GaussianBlurImageFilter::GaussianBlurImageFilter(float sigmaX, float sigmaY, TileMode tileMode)
    : sigmaX(sigmaX), sigmaY(sigmaY), tileMode(tileMode) {
}

static void Blur1D(std::unique_ptr<FragmentProcessor> source,
                   std::shared_ptr<RenderTargetProxy> renderTarget, float sigma,
                   GaussianBlurDirection direction, float stepLength, uint32_t renderFlags) {
  if (!renderTarget) {
    return;
  }
  auto drawingManager = renderTarget->getContext()->drawingManager();
  auto processor =
      GaussianBlur1DFragmentProcessor::Make(std::move(source), sigma, direction, stepLength);
  drawingManager->fillRTWithFP(renderTarget, std::move(processor), renderFlags);
}

static std::shared_ptr<TextureProxy> ScaleTexture(const TPArgs& args,
                                                  std::shared_ptr<TextureProxy> texture,
                                                  int targetWidth, int targetHeight) {
  auto renderTarget = RenderTargetProxy::MakeFallback(args.context, targetWidth, targetHeight,
                                                      texture->isAlphaOnly(), 1, args.mipmapped);
  if (!renderTarget) {
    return nullptr;
  }

  auto uvMatrix =
      Matrix::MakeScale(static_cast<float>(texture->width()) / static_cast<float>(targetWidth),
                        static_cast<float>(texture->height()) / static_cast<float>(targetHeight));
  auto finalProcessor = TextureEffect::Make(std::move(texture), {}, &uvMatrix);
  auto drawingManager = args.context->drawingManager();
  drawingManager->fillRTWithFP(renderTarget, std::move(finalProcessor), args.renderFlags);
  return renderTarget->getTextureProxy();
}

std::shared_ptr<TextureProxy> GaussianBlurImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& clipBounds, const TPArgs& args) const {
  const float maxSigma = std::max(sigmaX, sigmaY);
  float scaleFactor = 1.0f;
  bool blur2D = sigmaX > 0 && sigmaY > 0;

  Rect boundsWillSample = clipBounds;
  if (blur2D) {
    // if blur2D, we need to make sure the pixels are in the clip bounds while blur y.
    // if blur1D, we use the origin image.
    boundsWillSample = filterBounds(boundsWillSample);
    boundsWillSample.intersect(filterBounds(Rect::MakeWH(source->width(), source->height())));
    boundsWillSample.roundOut();
  }

  Rect scaledBounds = boundsWillSample;
  if (maxSigma > MAX_BLUR_SIGMA) {
    scaleFactor = MAX_BLUR_SIGMA / maxSigma;
    Matrix matrix = Matrix::MakeScale(scaleFactor);
    matrix.mapRect(&scaledBounds);
  }
  scaledBounds.roundOut();

  const auto isAlphaOnly = source->isAlphaOnly();
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(scaledBounds.width()), static_cast<int>(scaledBounds.height()),
      isAlphaOnly, 1, args.mipmapped);
  if (!renderTarget) {
    return nullptr;
  }

  Matrix uvMatrix = Matrix::MakeTrans(scaledBounds.left, scaledBounds.top);
  uvMatrix.postScale(boundsWillSample.width() / scaledBounds.width(),
                     boundsWillSample.height() / scaledBounds.height());
  FPArgs fpArgs(args.context, args.renderFlags,
                Rect::MakeWH(scaledBounds.width(), scaledBounds.height()));

  auto sourceProcessor = FragmentProcessor::Make(source, fpArgs, tileMode, tileMode, {}, &uvMatrix);

  if (blur2D) {
    Blur1D(std::move(sourceProcessor), renderTarget, sigmaX * scaleFactor,
           GaussianBlurDirection::Horizontal, 1.0f, args.renderFlags);

    // blur and scale the texture to the clip bounds.
    uvMatrix = Matrix::MakeScale(scaledBounds.width() / boundsWillSample.width(),
                                 scaledBounds.height() / boundsWillSample.height());
    uvMatrix.preTranslate(clipBounds.left - boundsWillSample.left,
                          clipBounds.top - boundsWillSample.top);

    sourceProcessor = TiledTextureEffect::Make(renderTarget->getTextureProxy(), tileMode, tileMode,
                                               {}, &uvMatrix);

    renderTarget = RenderTargetProxy::MakeFallback(
        args.context, static_cast<int>(clipBounds.width()), static_cast<int>(clipBounds.height()),
        isAlphaOnly, 1, args.mipmapped);

    if (!renderTarget) {
      return nullptr;
    }

    Blur1D(std::move(sourceProcessor), renderTarget, sigmaY * scaleFactor,
           GaussianBlurDirection::Vertical, boundsWillSample.height() / scaledBounds.height(),
           args.renderFlags);
    return renderTarget->getTextureProxy();
  }

  if (sigmaX > 0) {
    Blur1D(std::move(sourceProcessor), renderTarget, sigmaX * scaleFactor,
           GaussianBlurDirection::Horizontal, 1.0f, args.renderFlags);
  } else if (sigmaY > 0) {
    Blur1D(std::move(sourceProcessor), renderTarget, sigmaY * scaleFactor,
           GaussianBlurDirection::Vertical, 1.0f, args.renderFlags);
  }

  if (maxSigma <= MAX_BLUR_SIGMA) {
    return renderTarget->getTextureProxy();
  }

  return ScaleTexture(args, renderTarget->getTextureProxy(), static_cast<int>(clipBounds.width()),
                      static_cast<int>(clipBounds.height()));
}

Rect GaussianBlurImageFilter::onFilterBounds(const Rect& srcRect) const {
  return srcRect.makeOutset(2.f * sigmaX, 2.f * sigmaY);
}

std::unique_ptr<FragmentProcessor> GaussianBlurImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, uvMatrix);
}

}  // namespace tgfx