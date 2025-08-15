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
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "core/utils/MathExtra.h"

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

static std::shared_ptr<TextureProxy> RecreateTexture(const TPArgs& args,
                                                     std::shared_ptr<TextureProxy> source,
                                                     int targetWidth, int targetHeight,
                                                     const Matrix* uvMatrix) {
  auto renderTarget =
      RenderTargetProxy::MakeFallback(args.context, targetWidth, targetHeight, source->isAlphaOnly(),
                                      1, args.mipmapped, ImageOrigin::TopLeft, BackingFit::Approx);
  if (!renderTarget) {
    return nullptr;
  }

  auto finalProcessor = TextureEffect::Make(std::move(source), {}, uvMatrix);
  auto drawingManager = args.context->drawingManager();
  drawingManager->fillRTWithFP(renderTarget, std::move(finalProcessor), args.renderFlags);
  return renderTarget->asTextureProxy();
}

std::shared_ptr<TextureProxy> GaussianBlurImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& clipBounds, const TPArgs& args) const {

  Rect srcSampleBounds = clipBounds;
  // The pixels involved in the convolution operation may be outside the clipping area.
  srcSampleBounds = filterBounds(srcSampleBounds);
  srcSampleBounds.intersect(filterBounds(Rect::MakeWH(source->width(), source->height())));
  // Expand outward to prevent loss of intermediate state data.
  srcSampleBounds.roundOut();
  // Sample buounds left bottom offset from clip bounds.
  Point sampleBoundsLBOffset(srcSampleBounds.left - clipBounds.left, srcSampleBounds.bottom - clipBounds.bottom);

  Size dstDrawSize(clipBounds.width(), clipBounds.height());
  const float drawScale = std::max(0.0f, args.drawScale);
  if (!FloatNearlyEqual(drawScale, 1.0)) {
    dstDrawSize.scale(drawScale, drawScale);
  }
  // Shrink inward to ensure that all target texture points have corresponding sampling points in the original data.
  const ISize dstDrawISize = dstDrawSize.toFloor();

  Point sigma(blurrinessX, blurrinessY);
  Point blurDstScaleFactor(1.0f, 1.0f);
  bool isDrawScaleDown = (drawScale < 1.0f);
  if (isDrawScaleDown) {
    // Reduce the size of the blur target to improve computation speed.
    blurDstScaleFactor = Point(drawScale, drawScale);
    sigma *= drawScale;
  }
  if (sigma.x > MAX_BLUR_SIGMA) {
    blurDstScaleFactor.x *= MAX_BLUR_SIGMA / sigma.x;
    sigma.x = MAX_BLUR_SIGMA;
  }
  if (sigma.y > MAX_BLUR_SIGMA) {
    blurDstScaleFactor.y *= MAX_BLUR_SIGMA / sigma.y;
    sigma.y = MAX_BLUR_SIGMA;
  }
  Point blurDstToDrawDstScale(1.0f / blurDstScaleFactor.x, 1.0f / blurDstScaleFactor.y);
  blurDstToDrawDstScale.scale(drawScale, drawScale);

  Size blurDstSize(clipBounds.width(), clipBounds.height());
  if (blurDstScaleFactor.x < 1.0f || blurDstScaleFactor.y < 1.0f) {
    blurDstSize.scale(blurDstScaleFactor.x, blurDstScaleFactor.y);
  }
  // Expand outward to prevent loss of intermediate state data.
  const ISize blurDstISize = blurDstSize.toCeil();

  const auto isAlphaOnly = source->isAlphaOnly();
  bool blur2D = (sigma.x > 0.0f && sigma.y > 0.0f);
  std::shared_ptr<RenderTargetProxy> blurTarget = nullptr;
  bool isBlurDstScaled =
      (dstDrawISize.width != blurDstISize.width || dstDrawISize.height != blurDstISize.height);
  bool blurTargetMipmaped = (args.mipmapped && !isBlurDstScaled);
  if (blur2D) {
    auto sourceFragment = getSourceFragmentProcessor(source, args.context, args.renderFlags,
                                                     srcSampleBounds, blurDstScaleFactor);

    // Blur X
    // The texture size generated by the first blur must be able to accozmmodate all the pixels required for the
    // subsequent Y-axis convolution operation.
    Size xBlurDstSize(srcSampleBounds.width(), srcSampleBounds.height());
    if (blurDstScaleFactor.x < 1.0f || blurDstScaleFactor.y < 1.0f) {
      xBlurDstSize.scale(blurDstScaleFactor.x, blurDstScaleFactor.y);
    }
    // Expand outward to prevent loss of intermediate state data.
    const ISize xBlurDstISize = xBlurDstSize.toCeil();
    blurTarget = RenderTargetProxy::MakeFallback(args.context, xBlurDstISize.width,
                                                 xBlurDstISize.height, isAlphaOnly, 1, false,
                                                 ImageOrigin::TopLeft, BackingFit::Approx);
    if (blurTarget == nullptr) {
      return nullptr;
    }
    Blur1D(std::move(sourceFragment), blurTarget, sigma.x, GaussianBlurDirection::Horizontal, 1.0f,
           args.renderFlags);

    // Blur y
    // The final blur does not require expanding any additional area.
    // In the final stage, the texture needs to be cropped to match the output size of the entire blur operation. Align
    // origin by setting the texture sampling offset.
    auto uvMatrix = Matrix::MakeTrans(-sampleBoundsLBOffset.x, -sampleBoundsLBOffset.y);
    SamplingArgs samplingArgs = {tileMode, tileMode, {}, SrcRectConstraint::Fast};
    sourceFragment =
        TiledTextureEffect::Make(blurTarget->asTextureProxy(), samplingArgs, &uvMatrix);
    blurTarget = RenderTargetProxy::MakeFallback(
        args.context, blurDstISize.width, blurDstISize.height, isAlphaOnly, 1, blurTargetMipmaped,
        ImageOrigin::TopLeft, BackingFit::Approx);
    if (!blurTarget) {
      return nullptr;
    }
    Blur1D(std::move(sourceFragment), blurTarget, sigma.y, GaussianBlurDirection::Vertical, 1.0f,
           args.renderFlags);
  } else {
    // Crop source image and Align origin.
    Point srcSampleOffset(-sampleBoundsLBOffset.x, sampleBoundsLBOffset.y);
    auto sourceFragment = getSourceFragmentProcessor(source, args.context, args.renderFlags,
                                                     srcSampleBounds, blurDstScaleFactor, srcSampleOffset);

    blurTarget = RenderTargetProxy::MakeFallback(
        args.context, blurDstISize.width, blurDstISize.height, isAlphaOnly, 1, blurTargetMipmaped,
        ImageOrigin::TopLeft, BackingFit::Approx);
    auto blurDirection =
        (sigma.x > sigma.y ? GaussianBlurDirection::Horizontal : GaussianBlurDirection::Vertical);
    float blurSigma = std::max(sigma.x, sigma.y);
    Blur1D(std::move(sourceFragment), blurTarget, blurSigma, blurDirection, 1.0f,
           args.renderFlags);
  }

  if (isBlurDstScaled) {
    auto uvMatrix = Matrix::MakeScale(1.0f / blurDstToDrawDstScale.x, 1.0f / blurDstToDrawDstScale.y);
    return RecreateTexture(args, blurTarget->asTextureProxy(), dstDrawISize.width, dstDrawISize.height, &uvMatrix);
  } else {
    return blurTarget->asTextureProxy();
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
    const Point& scales, const Point& srcSampleOffset) const {
  Matrix uvMatrix = Matrix::MakeScale(1 / scales.x, 1 / scales.y);
  uvMatrix.postTranslate(drawRect.left + srcSampleOffset.x, drawRect.top + srcSampleOffset.y);
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
