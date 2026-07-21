/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "TentBlurImageFilter.h"
#include <memory>
#include <utility>
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/TentBlur1DFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

// Cap the tent blur radius to avoid downsampling at large radii, which introduces non-uniform
// X/Y step rounding and worsens the separable-kernel anisotropy.
#define MAX_TENT_RADIUS 64.f

float TentBlurImageFilter::MaxRadius() {
  return MAX_TENT_RADIUS;
}

TentBlurImageFilter::TentBlurImageFilter(float radiusX, float radiusY, TileMode tileMode)
    : radiusX(radiusX), radiusY(radiusY), tileMode(tileMode) {
}

static void TentBlur1D(PlacementPtr<FragmentProcessor> source,
                       std::shared_ptr<RenderTargetProxy> renderTarget, float radius,
                       TentBlurDirection direction, float stepLength, uint32_t renderFlags,
                       bool inputIsPacked = false) {
  if (!renderTarget) {
    return;
  }
  auto context = renderTarget->getContext();
  auto drawingManager = context->drawingManager();
  auto processor =
      TentBlur1DFragmentProcessor::Make(context->drawingAllocator(), std::move(source), radius,
                                        direction, stepLength, MAX_TENT_RADIUS, inputIsPacked);
  drawingManager->fillRTWithFP(std::move(renderTarget), std::move(processor), renderFlags);
}

std::shared_ptr<TextureProxy> TentBlurImageFilter::lockTextureProxy(std::shared_ptr<Image> source,
                                                                    const Rect& clipBounds,
                                                                    const TPArgs& args) const {
  Rect srcSampleBounds = clipBounds;
  srcSampleBounds = filterBounds(srcSampleBounds);
  srcSampleBounds.intersect(filterBounds(Rect::MakeWH(source->width(), source->height())));
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

  float radX = radiusX;
  float radY = radiusY;
  const bool isDrawScaleDown = (drawScaleX < 1.0f || drawScaleY < 1.0f);
  if (isDrawScaleDown) {
    radX *= drawScaleX;
    radY *= drawScaleY;
  }
  radX = std::min(radX, MAX_TENT_RADIUS);
  radY = std::min(radY, MAX_TENT_RADIUS);
  const bool blur2D = (radX > 0.0f && radY > 0.0f);

  float blurDstScaleX = (radiusX > 0.0f ? radX / radiusX : 1.0f);
  float blurDstScaleY = (radiusY > 0.0f ? radY / radiusY : 1.0f);
  Rect scaledSrcSampleBounds = srcSampleBounds;
  scaledSrcSampleBounds.scale(blurDstScaleX, blurDstScaleY);
  scaledSrcSampleBounds.roundOut();
  const float blurDstWidth = scaledSrcSampleBounds.width();
  const float blurDstHeight = scaledSrcSampleBounds.height();
  blurDstScaleX = blurDstWidth / srcSampleBounds.width();
  blurDstScaleY = blurDstHeight / srcSampleBounds.height();

  PlacementPtr<FragmentProcessor> sourceFragment = getSourceFragmentProcessor(
      source, args.context, args.renderFlags, srcSampleBounds, Point(blurDstScaleX, blurDstScaleY));
  const auto isAlphaOnly = source->isAlphaOnly();
  const bool isBlurDstScaled = (!FloatNearlyEqual(blurDstWidth, dstDrawWidth) ||
                                !FloatNearlyEqual(blurDstHeight, dstDrawHeight));
  const bool isBlurDstTrans =
      clipBounds.left == srcSampleBounds.left && clipBounds.top == srcSampleBounds.top;
  const bool needExtraTransform = isBlurDstScaled || isBlurDstTrans;
  const bool defaultBlurTargetMipmapped = (args.mipmapped && !blur2D && !needExtraTransform);
  auto renderTarget = RenderTargetProxy::Make(
      args.context, static_cast<int>(blurDstWidth), static_cast<int>(blurDstHeight), isAlphaOnly, 1,
      defaultBlurTargetMipmapped, ImageOrigin::TopLeft,
      blur2D || needExtraTransform ? BackingFit::Approx : args.backingFit);
  if (!renderTarget) {
    return nullptr;
  }

  auto allocator = args.context->drawingAllocator();
  if (blur2D) {
    TentBlur1D(std::move(sourceFragment), renderTarget, radX, TentBlurDirection::Horizontal, 1.0f,
               args.renderFlags, false);

    SamplingArgs samplingArgs = {tileMode, tileMode, {}, SrcRectConstraint::Fast};
    sourceFragment =
        TiledTextureEffect::Make(allocator, renderTarget->asTextureProxy(), samplingArgs);
    const bool finalBlurTargetMipmapped = (args.mipmapped && !needExtraTransform);
    renderTarget = RenderTargetProxy::Make(
        args.context, static_cast<int>(blurDstWidth), static_cast<int>(blurDstHeight), isAlphaOnly,
        1, finalBlurTargetMipmapped, ImageOrigin::TopLeft,
        needExtraTransform ? BackingFit::Approx : args.backingFit);
    if (!renderTarget) {
      return nullptr;
    }
    // The second pass input is the first pass output, which is an RGBA8 render target with float
    // values packed into 4 channels. inputIsPacked=true tells the shader to unpack via
    // dot(rgba, UNPACK) to recover float precision lost in the RGBA8 intermediate texture.
    TentBlur1D(std::move(sourceFragment), renderTarget, radY, TentBlurDirection::Vertical, 1.0f,
               args.renderFlags, true);
  } else {
    const auto blurDirection =
        (radX > radY ? TentBlurDirection::Horizontal : TentBlurDirection::Vertical);
    const float blurRadius = std::max(radX, radY);
    TentBlur1D(std::move(sourceFragment), renderTarget, blurRadius, blurDirection, 1.0f,
               args.renderFlags);
  }

  if (!needExtraTransform) {
    return renderTarget->asTextureProxy();
  }

  auto finalUVMatrix = Matrix::MakeScale(clipBounds.width() * blurDstScaleX / dstDrawWidth,
                                         clipBounds.height() * blurDstScaleY / dstDrawHeight);
  finalUVMatrix.postTranslate((clipBounds.left - srcSampleBounds.left) * blurDstScaleX,
                              (clipBounds.top - srcSampleBounds.top) * blurDstScaleY);
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

Rect TentBlurImageFilter::onFilterBounds(const Rect& rect, MapDirection) const {
  return rect.makeOutset(radiusX, radiusY);
}

PlacementPtr<FragmentProcessor> TentBlurImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, constraint, uvMatrix);
}

PlacementPtr<FragmentProcessor> TentBlurImageFilter::getSourceFragmentProcessor(
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
  // TentBlur1DFragmentProcessor samples its child via emitChild with a coordFunc, which requires
  // the child to have exactly 1 CoordTransform. If the source FP has multiple transforms (e.g.,
  // certain TiledTextureEffect modes), rasterize to a render target first and wrap as a simple
  // TiledTextureEffect with a single transform.
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
