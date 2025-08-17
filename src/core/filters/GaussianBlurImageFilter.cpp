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

static std::shared_ptr<TextureProxy> ScaleTexture(const TPArgs& args,
                                                  std::shared_ptr<TextureProxy> proxy,
                                                  int targetWidth, int targetHeight) {
  auto renderTarget =
      RenderTargetProxy::MakeFallback(args.context, targetWidth, targetHeight, proxy->isAlphaOnly(),
                                      1, args.mipmapped, ImageOrigin::TopLeft, BackingFit::Approx);
  if (!renderTarget) {
    return nullptr;
  }

  auto uvMatrix =
      Matrix::MakeScale(static_cast<float>(proxy->width()) / static_cast<float>(targetWidth),
                        static_cast<float>(proxy->height()) / static_cast<float>(targetHeight));
  auto finalProcessor = TextureEffect::Make(std::move(proxy), {}, &uvMatrix);
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

  Size dstDrawSize(clipBounds.width(), clipBounds.height());
  const float drawScale = std::max(0.0f, args.drawScale);
  if (!FloatNearlyEqual(drawScale, 1.0f)) {
    dstDrawSize.scale(drawScale, drawScale);
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
      
      float blurDstScaleX = (blurrinessX > 0.0f ? sigmaX / blurrinessX : 1.0f);
      float blurDstScaleY = (blurrinessY > 0.0f ? sigmaY / blurrinessY : 1.0f);
      Size blurDstSize(std::ceil(clipBounds.width() * blurDstScaleX), std::ceil(clipBounds.height() * blurDstScaleY));
      ISize blurDstSizeI(static_cast<int>(blurDstSize.width), static_cast<int>(blurDstSize.height));
      blurDstScaleX = blurDstSize.width / clipBounds.width();
      blurDstScaleY = blurDstSize.height / clipBounds.height();
      
      

  const auto isAlphaOnly = source->isAlphaOnly();
  bool blur2D = (sigmaX > 0.0f && sigmaY > 0.0f);
  std::shared_ptr<RenderTargetProxy> blurTarget = nullptr;
  bool isBlurDstScaled =
      (dstDrawSizeI.width != blurDstSizeI.width || dstDrawSizeI.height != blurDstSizeI.height);
  bool blurTargetMipmaped = (args.mipmapped && !isBlurDstScaled);
  if (blur2D) {
    //    auto sourceFragment = getSourceFragmentProcessor(source, args.context, args.renderFlags,
    //                                                     srcSampleBounds, blurDstScaleFactor);
    //
    //    // Blur X
    //    // The texture size generated by the first blur must be able to accozmmodate all the pixels required for the
    //    // subsequent Y-axis convolution operation.
    //    Size xBlurDstSize(srcSampleBounds.width(), srcSampleBounds.height());
    //    if (blurDstScaleFactor.x < 1.0f || blurDstScaleFactor.y < 1.0f) {
    //      xBlurDstSize.scale(blurDstScaleFactor.x, blurDstScaleFactor.y);
    //    }
    //    // Expand outward to prevent loss of intermediate state data.
    //    const ISize xBlurDstISize = xBlurDstSize.toCeil();
    //    blurTarget = RenderTargetProxy::MakeFallback(args.context, xBlurDstISize.width,
    //                                                 xBlurDstISize.height, isAlphaOnly, 1, false,
    //                                                 ImageOrigin::TopLeft, BackingFit::Approx);
    //    if (blurTarget == nullptr) {
    return nullptr;
    //    }
    //    Blur1D(std::move(sourceFragment), blurTarget, sigma.x, GaussianBlurDirection::Horizontal, 1.0f,
    //           args.renderFlags);
    //
    //    // Blur y
    //    // The final blur does not require expanding any additional area.
    //    // In the final stage, the texture needs to be cropped to match the output size of the entire blur operation. Align
    //    // origin by setting the texture sampling offset.
    //    auto uvMatrix = Matrix::MakeTrans(-sampleBoundsLBOffset.x, -sampleBoundsLBOffset.y);
    //    SamplingArgs samplingArgs = {tileMode, tileMode, {}, SrcRectConstraint::Fast};
    //    sourceFragment =
    //        TiledTextureEffect::Make(blurTarget->asTextureProxy(), samplingArgs, &uvMatrix);
    //    blurTarget = RenderTargetProxy::MakeFallback(
    //        args.context, blurDstISize.width, blurDstISize.height, isAlphaOnly, 1, blurTargetMipmaped,
    //        ImageOrigin::TopLeft, BackingFit::Approx);
    //    if (!blurTarget) {
    //      return nullptr;
    //    }
    //    Blur1D(std::move(sourceFragment), blurTarget, sigma.y, GaussianBlurDirection::Vertical, 1.0f,
    //           args.renderFlags);
  } else {
    // Crop source image and Align origin.
    Rect scaledSrcSampleBounds = srcSampleBounds;
    scaledSrcSampleBounds.scale(blurDstScaleX, blurDstScaleY);
    float blurDrawLeft = (blurDstSize.width - scaledSrcSampleBounds.width()) * 0.5f;
    float blurDrawTop = (blurDstSize.height - scaledSrcSampleBounds.height()) * 0.5f;
    float blurDrawRight = blurDrawLeft + scaledSrcSampleBounds.width();
    float blurDrawBottom = blurDrawTop + scaledSrcSampleBounds.height();
    Rect blurDrawRect(blurDrawLeft, blurDrawTop, blurDrawRight, blurDrawBottom);
    blurDrawRect.roundOut();
    
    auto sourceFragment = getSourceFragmentProcessor(source, args.context, args.renderFlags, srcSampleBounds, blurDrawRect);
    blurTarget = RenderTargetProxy::MakeFallback(
        args.context, blurDstSizeI.width, blurDstSizeI.height, isAlphaOnly, 1, blurTargetMipmaped,
        ImageOrigin::TopLeft, BackingFit::Approx);
     auto blurDirection =
        (sigmaX > sigmaY ? GaussianBlurDirection::Horizontal : GaussianBlurDirection::Vertical);
    float blurSigma = std::max(sigmaX, sigmaY);
    
    Blur1D(std::move(sourceFragment), blurTarget, blurSigma, blurDirection, 1.0f, args.renderFlags);
  }

  if (isBlurDstScaled) {
    return ScaleTexture(args, blurTarget->asTextureProxy(), blurDstSizeI.width, blurDstSizeI.height);
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
    std::shared_ptr<Image> source, Context* context, uint32_t renderFlags, const Rect& srcSampleRect,
    const Rect& drawRect) const {
  float drawWidth = drawRect.width();
  float drawHeight = drawRect.height();
  float srcToDrawScaleX = srcSampleRect.width() / drawWidth;
  float srcToDrawScaleY = srcSampleRect.height() / drawHeight;
  Matrix uvMatrix = Matrix::MakeScale(srcToDrawScaleX, srcToDrawScaleY);
  uvMatrix.postTranslate(srcSampleRect.left, srcSampleRect.top);
      uvMatrix.preTranslate(-drawRect.left, -drawRect.top);
  FPArgs args = FPArgs(context, renderFlags, drawRect, std::max(1.0f / srcToDrawScaleX, 1.0f / srcToDrawScaleY));

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
      context, static_cast<int>(drawRect.width()), static_cast<int>(drawRect.height()),
      source->isAlphaOnly(), 1);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  context->drawingManager()->fillRTWithFP(renderTarget, std::move(fp), renderFlags);
  return TiledTextureEffect::Make(renderTarget->asTextureProxy(), samplingArgs);
}

}  // namespace tgfx
