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
#include "gpu/processors/GaussianBlur2DFragmentProcessor.h"
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

// The single pass evaluates (2 * ceil(2 * sigma) + 1)^2 samples per pixel. Together with MAX_SIGMA
// this cap keeps a pixel at 81 samples and a 64x64 target at about 330k samples, the budget within
// which the extra sampling stays below the cost of the render target and the pass it replaces.
static constexpr int SINGLE_PASS_MAX_EDGE = 64;

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
      context->drawingAllocator(), std::move(source), sigma, direction, stepLength, MAX_BLUR_SIGMA);
  drawingManager->fillRTWithFP(std::move(renderTarget), std::move(processor), renderFlags);
}

std::shared_ptr<TextureProxy> GaussianBlurImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& clipBounds, const TPArgs& args) const {
  // The single pass and the folded crop read the source through whole-pixel offsets, so a clip that
  // reaches past the source's blur range would make the two paths disagree along the columns.
  DEBUG_ASSERT(filterBounds(Rect::MakeWH(source->width(), source->height())).contains(clipBounds));
  Rect srcSampleBounds = clipBounds;
  // The pixels involved in the convolution operation may be outside the clipping area.
  srcSampleBounds = filterBounds(srcSampleBounds);
  srcSampleBounds.intersect(filterBounds(Rect::MakeWH(source->width(), source->height())));
  // Expand outward to prevent loss of intermediate state data.
  srcSampleBounds.roundOut();

  float dstDrawWidth = clipBounds.width();
  float dstDrawHeight = clipBounds.height();
  float drawScaleX = std::max(0.0f, args.drawScale);
  float drawScaleY = drawScaleX;
  if (!FloatNearlyEqual(drawScaleX, 1.0f)) {
    dstDrawWidth *= drawScaleX;
    dstDrawHeight *= drawScaleY;
  }
  dstDrawWidth = std::ceil(dstDrawWidth);
  dstDrawHeight = std::ceil(dstDrawHeight);
  drawScaleX = dstDrawWidth / clipBounds.width();
  drawScaleY = dstDrawHeight / clipBounds.height();

  float sigmaX = blurrinessX;
  float sigmaY = blurrinessY;
  const bool isDrawScaleDown = (drawScaleX < 1.0f || drawScaleY < 1.0f);
  if (isDrawScaleDown) {
    // Reduce the size of the blur target to improve computation speed.
    sigmaX *= drawScaleX;
    sigmaY *= drawScaleY;
  }
  sigmaX = std::min(sigmaX, MAX_BLUR_SIGMA);
  sigmaY = std::min(sigmaY, MAX_BLUR_SIGMA);
  const bool blur2D = (sigmaX > 0.0f && sigmaY > 0.0f);

  // BlurDstScale describes the scaling factor of the Gaussian blur render target size relative to the size of the
  // source data clip bounds.
  float blurDstScaleX = (blurrinessX > 0.0f ? sigmaX / blurrinessX : 1.0f);
  float blurDstScaleY = (blurrinessY > 0.0f ? sigmaY / blurrinessY : 1.0f);
  Rect scaledSrcSampleBounds = srcSampleBounds;
  scaledSrcSampleBounds.scale(blurDstScaleX, blurDstScaleY);
  scaledSrcSampleBounds.roundOut();
  // The entire process involves texture upscaling, and linear filtering is used to avoid aliasing. This causes the
  // edge pixels of the target texture to be computed by blending the surrounding pixels of the corresponding sampling
  // points in the source texture. In a tiled rendering scenario, the edges of each tile need to blend with the edge
  // pixels of adjacent tiles to ensure smooth transitions between them. Therefore, the data region contained in
  // intermediate textures must be larger than the actual clipped data region.
  const float blurDstWidth = scaledSrcSampleBounds.width();
  const float blurDstHeight = scaledSrcSampleBounds.height();
  blurDstScaleX = blurDstWidth / srcSampleBounds.width();
  blurDstScaleY = blurDstHeight / srcSampleBounds.height();

  PlacementPtr<FragmentProcessor> sourceFragment = getSourceFragmentProcessor(
      source, args.context, args.renderFlags, srcSampleBounds, Point(blurDstScaleX, blurDstScaleY));
  const auto isAlphaOnly = source->isAlphaOnly();
  const bool isBlurDstScaled = (!FloatNearlyEqual(blurDstWidth, dstDrawWidth) ||
                                !FloatNearlyEqual(blurDstHeight, dstDrawHeight));
  auto finalUVMatrix = Matrix::MakeScale(clipBounds.width() * blurDstScaleX / dstDrawWidth,
                                         clipBounds.height() * blurDstScaleY / dstDrawHeight);
  finalUVMatrix.postTranslate((clipBounds.left - srcSampleBounds.left) * blurDstScaleX,
                              (clipBounds.top - srcSampleBounds.top) * blurDstScaleY);
  const bool isIdentityEnd = !isBlurDstScaled && finalUVMatrix.isIdentity();
  const bool needExtraTransform = !isIdentityEnd;
  // When the final UV matrix is a pure translation, the vertical pass applies that offset while it
  // reads the horizontal result and writes the requested bounds directly, so the separate copy is
  // unnecessary. The blur passes before it may still have rescaled the source.
  const bool foldCrop = blur2D && needExtraTransform && finalUVMatrix.isTranslate();
  // A small kernel is cheaper to evaluate in full than to split into passes. The single pass reads
  // the source directly, so it only matches the separable blur where both read the same pixels:
  // the vertical pass reads the horizontal result through the tile mode, which for rows past
  // srcSampleBounds agrees with reading the source only under Decal and Clamp.
  const auto kernelRadiusY = std::ceil(2.0f * sigmaY);
  const bool rowsInside = clipBounds.top - kernelRadiusY >= srcSampleBounds.top &&
                          clipBounds.bottom + kernelRadiusY <= srcSampleBounds.bottom;
  const bool singlePass =
      finalUVMatrix.isTranslate() && FloatNearlyEqual(blurDstScaleX, 1.0f) &&
      FloatNearlyEqual(blurDstScaleY, 1.0f) &&
      std::max(sigmaX, sigmaY) <= GaussianBlur2DFragmentProcessor::MAX_SIGMA &&
      std::max(dstDrawWidth, dstDrawHeight) <= static_cast<float>(SINGLE_PASS_MAX_EDGE) &&
      (!blur2D || tileMode == TileMode::Decal || tileMode == TileMode::Clamp || rowsInside);
  auto allocator = args.context->drawingAllocator();
  if (singlePass) {
    auto target = RenderTargetProxy::Make(args.context, static_cast<int>(dstDrawWidth),
                                          static_cast<int>(dstDrawHeight), isAlphaOnly, 1,
                                          args.mipmapped, ImageOrigin::TopLeft, args.backingFit);
    auto processor = GaussianBlur2DFragmentProcessor::Make(
        allocator, std::move(sourceFragment), sigmaX, sigmaY,
        Point::Make(finalUVMatrix.getTranslateX(), finalUVMatrix.getTranslateY()));
    if (target == nullptr || processor == nullptr) {
      return nullptr;
    }
    args.context->drawingManager()->fillRTWithFP(target, std::move(processor), args.renderFlags);
    return target->asTextureProxy();
  }
  const bool defaultBlurTargetMipmapped = (args.mipmapped && !blur2D && !needExtraTransform);
  auto renderTarget = RenderTargetProxy::Make(
      args.context, static_cast<int>(blurDstWidth), static_cast<int>(blurDstHeight), isAlphaOnly, 1,
      defaultBlurTargetMipmapped, ImageOrigin::TopLeft,
      blur2D || needExtraTransform ? BackingFit::Approx : args.backingFit);
  if (!renderTarget) {
    return nullptr;
  }

  if (blur2D) {
    Blur1D(std::move(sourceFragment), renderTarget, sigmaX, GaussianBlurDirection::Horizontal, 1.0f,
           args.renderFlags);

    SamplingArgs samplingArgs = {tileMode, tileMode, {}, SrcRectConstraint::Fast};
    sourceFragment = TiledTextureEffect::Make(allocator, renderTarget->asTextureProxy(),
                                              samplingArgs, foldCrop ? &finalUVMatrix : nullptr);
    // A folded pass writes the requested bounds, so its target takes what the copy target took.
    const bool isFinalTarget = foldCrop || !needExtraTransform;
    const auto finalWidth = static_cast<int>(foldCrop ? dstDrawWidth : blurDstWidth);
    const auto finalHeight = static_cast<int>(foldCrop ? dstDrawHeight : blurDstHeight);
    renderTarget = RenderTargetProxy::Make(args.context, finalWidth, finalHeight, isAlphaOnly, 1,
                                           args.mipmapped && isFinalTarget, ImageOrigin::TopLeft,
                                           isFinalTarget ? args.backingFit : BackingFit::Approx);
    if (!renderTarget) {
      return nullptr;
    }
    Blur1D(std::move(sourceFragment), renderTarget, sigmaY, GaussianBlurDirection::Vertical, 1.0f,
           args.renderFlags);
    if (foldCrop) {
      return renderTarget->asTextureProxy();
    }
  } else {
    const auto blurDirection =
        (sigmaX > sigmaY ? GaussianBlurDirection::Horizontal : GaussianBlurDirection::Vertical);
    const float blurSigma = std::max(sigmaX, sigmaY);
    Blur1D(std::move(sourceFragment), renderTarget, blurSigma, blurDirection, 1.0f,
           args.renderFlags);
  }

  if (!needExtraTransform) {
    return renderTarget->asTextureProxy();
  }

  auto finalProcessor =
      TextureEffect::Make(allocator, renderTarget->asTextureProxy(), {}, &finalUVMatrix);
  renderTarget = RenderTargetProxy::Make(args.context, static_cast<int>(dstDrawWidth),
                                         static_cast<int>(dstDrawHeight), isAlphaOnly, 1,
                                         args.mipmapped, ImageOrigin::TopLeft, args.backingFit);
  if (!renderTarget) {
    return nullptr;
  }
  const auto drawingManager = args.context->drawingManager();
  drawingManager->fillRTWithFP(renderTarget, std::move(finalProcessor), args.renderFlags);

  return renderTarget->asTextureProxy();
}

Rect GaussianBlurImageFilter::onFilterBounds(const Rect& rect, MapDirection) const {
  return rect.makeOutset(2.f * blurrinessX, 2.f * blurrinessY);
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
  auto renderTarget =
      RenderTargetProxy::Make(context, static_cast<int>(scaledDrawRect.width()),
                              static_cast<int>(scaledDrawRect.height()), source->isAlphaOnly(), 1);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  context->drawingManager()->fillRTWithFP(renderTarget, std::move(fp), renderFlags);
  auto allocator = context->drawingAllocator();
  return TiledTextureEffect::Make(allocator, renderTarget->asTextureProxy(), samplingArgs);
}

}  // namespace tgfx
