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

#include "GaussianBlurImageFilter.h"
#include <memory>
#include <utility>
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

// When a 1px wide line segment is scaled down to 0.25 in both width and height, it can still provide acceptable image
// information. However, when the sigma exceeds 40, the line segment becomes so blurred that it is almost invisible.
// Therefore, 10 is chosen as the MAX_BLUR_SIGMA.
#define MAX_BLUR_SIGMA 10.f

std::shared_ptr<ImageFilter> ImageFilter::Blur(float blurrinessX, float blurrinessY,
                                               TileMode tileMode) {
  if (blurrinessX < 0 || blurrinessY < 0 || (blurrinessX == 0 && blurrinessY == 0)) {
    return nullptr;
  }
  return std::make_shared<GaussianBlurImageFilter>(blurrinessX, blurrinessY, tileMode);
}

float GaussianBlurImageFilter::MaxSigma() {
  return MAX_BLUR_SIGMA;
}

GaussianBlurImageFilter::GaussianBlurImageFilter(float blurrinessX, float blurrinessY,
                                                 TileMode tileMode)
    : blurrinessX(blurrinessX), blurrinessY(blurrinessY), tileMode(tileMode) {
}

static void Blur1D(PlacementPtr<FragmentProcessor> source,
                   std::shared_ptr<RenderTargetProxy> renderTarget, float sigma,
                   GaussianBlurDirection direction, float stepLength, uint32_t renderFlags) {
  if (!renderTarget) {
    return;
  }
  auto context = renderTarget->getContext();
  auto drawingManager = context->drawingManager();
  auto processor = GaussianBlur1DFragmentProcessor::Make(
      context->drawingBuffer(), std::move(source), sigma, direction, stepLength, MAX_BLUR_SIGMA);
  drawingManager->fillRTWithFP(std::move(renderTarget), std::move(processor), renderFlags);
}

std::shared_ptr<TextureProxy> GaussianBlurImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& clipBounds, const TPArgs& args) const {
  Rect srcSampleBounds = clipBounds;
  // The pixels involved in the convolution operation may be outside the clipping area.
  srcSampleBounds = filterBounds(srcSampleBounds);
  srcSampleBounds.intersect(filterBounds(Rect::MakeWH(source->width(), source->height())));
  // Expand outward to prevent loss of intermediate state data.
  srcSampleBounds.roundOut();

  Size dstDrawSize(clipBounds.width(), clipBounds.height());
  const float drawScale = std::max(0.0f, args.drawScale);
  if (!FloatNearlyEqual(drawScale, 1.0f)) {
    dstDrawSize.width *= drawScale;
    dstDrawSize.height *= drawScale;
  }
  const ISize dstDrawSizeI = dstDrawSize.toCeil();

  float sigmaX = blurrinessX;
  float sigmaY = blurrinessY;
  bool isDrawScaleDown = (drawScale < 1.0f);
  if (isDrawScaleDown) {
    // Reduce the size of the blur target to improve computation speed.
    sigmaX *= drawScale;
    sigmaY *= drawScale;
  }
  sigmaX = std::min(sigmaX, MAX_BLUR_SIGMA);
  sigmaY = std::min(sigmaY, MAX_BLUR_SIGMA);
  bool blur2D = (sigmaX > 0.0f && sigmaY > 0.0f);

  // The process of Gaussian blur involves operations for texture upscaling, where ​​linear filtering​​ is used during
  // sampling to prevent aliasing artifacts. In a tile-based rendering scenario, the edges of each tile need to blend
  // with pixels from adjacent tiles to ensure smooth gradient transitions between neighboring tiles. Therefore, the
  // intermediate texture data must be ​​extended outward​​ beyond the cropped region.
  float blurDstScaleX = (blurrinessX > 0.0f ? sigmaX / blurrinessX : 1.0f);
  float blurDstScaleY = (blurrinessY > 0.0f ? sigmaY / blurrinessY : 1.0f);
  Rect scaledSrcSampleBounds = srcSampleBounds;
  scaledSrcSampleBounds.scale(blurDstScaleX, blurDstScaleY);
  scaledSrcSampleBounds.roundOut();
  Size blurDstSize(scaledSrcSampleBounds.width(), scaledSrcSampleBounds.height());
  ISize blurDstSizeI(static_cast<int>(blurDstSize.width), static_cast<int>(blurDstSize.height));
  blurDstScaleX = blurDstSize.width / srcSampleBounds.width();
  blurDstScaleY = blurDstSize.height / srcSampleBounds.height();

  PlacementPtr<FragmentProcessor> sourceFragment = getSourceFragmentProcessor(
      source, args.context, args.renderFlags, srcSampleBounds, Point(blurDstScaleX, blurDstScaleY));
  const auto isAlphaOnly = source->isAlphaOnly();
  bool isBlurDstScaled =
      (blurDstSizeI.width != dstDrawSizeI.width || blurDstSizeI.height != dstDrawSizeI.height);
  bool defaultBlurTargetMipmapped = (args.mipmapped && !blur2D && !isBlurDstScaled);
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, blurDstSizeI.width, blurDstSizeI.height, isAlphaOnly, 1,
      defaultBlurTargetMipmapped, ImageOrigin::TopLeft, BackingFit::Approx);
  if (!renderTarget) {
    return nullptr;
  }

  if (blur2D) {
    Blur1D(std::move(sourceFragment), renderTarget, sigmaX, GaussianBlurDirection::Horizontal, 1.0f,
           args.renderFlags);

    SamplingArgs samplingArgs = {tileMode, tileMode, {}, SrcRectConstraint::Fast};
    sourceFragment = TiledTextureEffect::Make(renderTarget->asTextureProxy(), samplingArgs);
    bool finalBlurTargetMipmapped = (args.mipmapped && !isBlurDstScaled);
    renderTarget = RenderTargetProxy::MakeFallback(
        args.context, blurDstSizeI.width, blurDstSizeI.height, isAlphaOnly, 1,
        finalBlurTargetMipmapped, ImageOrigin::TopLeft, BackingFit::Approx);
    if (!renderTarget) {
      return nullptr;
    }
    Blur1D(std::move(sourceFragment), renderTarget, sigmaY, GaussianBlurDirection::Vertical, 1.0f,
           args.renderFlags);
  } else {
    auto blurDirection =
        (sigmaX > sigmaY ? GaussianBlurDirection::Horizontal : GaussianBlurDirection::Vertical);
    float blurSigma = std::max(sigmaX, sigmaY);
    Blur1D(std::move(sourceFragment), renderTarget, blurSigma, blurDirection, 1.0f,
           args.renderFlags);
  }

  if (isBlurDstScaled) {
    auto finalUVMatrix =
        Matrix::MakeScale(clipBounds.width() * blurDstScaleX / dstDrawSize.width,
                          clipBounds.height() * blurDstScaleY / dstDrawSize.height);
    finalUVMatrix.postTranslate((clipBounds.left - srcSampleBounds.left) * blurDstScaleX,
                                (clipBounds.top - srcSampleBounds.top) * blurDstScaleY);
    auto finalProcessor = TextureEffect::Make(renderTarget->asTextureProxy(), {}, &finalUVMatrix);
    renderTarget = RenderTargetProxy::MakeFallback(
        args.context, dstDrawSizeI.width, dstDrawSizeI.height, isAlphaOnly, 1, args.mipmapped,
        ImageOrigin::TopLeft, BackingFit::Approx);
    if (!renderTarget) {
      return nullptr;
    }
    auto drawingManager = args.context->drawingManager();
    drawingManager->fillRTWithFP(renderTarget, std::move(finalProcessor), args.renderFlags);
    return renderTarget->asTextureProxy();
  } else {
    return renderTarget->asTextureProxy();
  }
}

Rect GaussianBlurImageFilter::onFilterBounds(const Rect& srcRect) const {
  return srcRect.makeOutset(2.f * blurrinessX, 2.f * blurrinessY);
}

PlacementPtr<FragmentProcessor> GaussianBlurImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

PlacementPtr<FragmentProcessor> GaussianBlurImageFilter::getSourceFragmentProcessor(
    std::shared_ptr<Image> source, Context* context, uint32_t renderFlags, const Rect& drawRect,
    const Point& scales) const {
  Matrix uvMatrix = Matrix::MakeScale(1 / scales.x, 1 / scales.y);
  uvMatrix.postTranslate(drawRect.left, drawRect.top);
  auto scaledDrawRect = drawRect;
  scaledDrawRect.scale(scales.x, scales.y);
  scaledDrawRect.round();
  FPArgs args =
      FPArgs(context, renderFlags, Rect::MakeWH(scaledDrawRect.width(), scaledDrawRect.height()),
             std::max(scales.x, scales.y));

  SamplingArgs samplingArgs = {};
  samplingArgs.tileModeX = tileMode;
  samplingArgs.tileModeY = tileMode;
  auto fp = FragmentProcessor::Make(source, args, samplingArgs, &uvMatrix);
  if (fp == nullptr) {
    return nullptr;
  }
  if (fp->numCoordTransforms() == 1) {
    return fp;
  }
  auto renderTarget = RenderTargetProxy::MakeFallback(
      context, static_cast<int>(scaledDrawRect.width()), static_cast<int>(scaledDrawRect.height()),
      source->isAlphaOnly(), 1);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  context->drawingManager()->fillRTWithFP(renderTarget, std::move(fp), renderFlags);
  return TiledTextureEffect::Make(renderTarget->asTextureProxy(), samplingArgs);
}

}  // namespace tgfx
