/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "BlurImageFilter.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/DualBlurFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
static const float BLUR_LEVEL_1_LIMIT = 10.0f;
static const float BLUR_LEVEL_2_LIMIT = 15.0f;
static const float BLUR_LEVEL_3_LIMIT = 55.0f;
static const float BLUR_LEVEL_4_LIMIT = 120.0f;
static const float BLUR_LEVEL_5_LIMIT = 300.0f;

static const float BLUR_LEVEL_MAX_LIMIT = BLUR_LEVEL_5_LIMIT;

static const int BLUR_LEVEL_1_DEPTH = 1;
static const int BLUR_LEVEL_2_DEPTH = 2;
static const int BLUR_LEVEL_3_DEPTH = 2;
static const int BLUR_LEVEL_4_DEPTH = 3;
static const int BLUR_LEVEL_5_DEPTH = 3;

static const float BLUR_LEVEL_1_SCALE = 1.0f;
static const float BLUR_LEVEL_2_SCALE = 0.8f;
static const float BLUR_LEVEL_3_SCALE = 0.5f;
static const float BLUR_LEVEL_4_SCALE = 0.5f;
static const float BLUR_LEVEL_5_SCALE = 0.5f;

static const float BLUR_STABLE = 10.0f;

static std::tuple<int, float, float, float> Get(float blurriness) {
  auto scaleFactor = 1.0f;
  if (blurriness > BLUR_LEVEL_MAX_LIMIT) {
    scaleFactor = BLUR_LEVEL_MAX_LIMIT / blurriness;
    blurriness = BLUR_LEVEL_MAX_LIMIT;
  }
  if (blurriness < BLUR_LEVEL_1_LIMIT) {
    return {BLUR_LEVEL_1_DEPTH, BLUR_LEVEL_1_SCALE, blurriness / BLUR_LEVEL_1_LIMIT * 1.2,
            scaleFactor};
  } else if (blurriness < BLUR_LEVEL_2_LIMIT) {
    return {BLUR_LEVEL_2_DEPTH, BLUR_LEVEL_2_SCALE,
            0.48 + (blurriness - BLUR_STABLE) / (BLUR_LEVEL_2_LIMIT - BLUR_STABLE) * 0.4,
            scaleFactor};
  } else if (blurriness < BLUR_LEVEL_3_LIMIT) {
    return {BLUR_LEVEL_3_DEPTH, BLUR_LEVEL_3_SCALE,
            (blurriness - BLUR_STABLE) / (BLUR_LEVEL_3_LIMIT - BLUR_STABLE) * 5.0, scaleFactor};
  } else if (blurriness < BLUR_LEVEL_4_LIMIT) {
    return {BLUR_LEVEL_4_DEPTH, BLUR_LEVEL_4_SCALE,
            (blurriness - BLUR_STABLE) / (BLUR_LEVEL_4_LIMIT - BLUR_STABLE) * 6.0, scaleFactor};
  } else {
    return {
        BLUR_LEVEL_5_DEPTH, BLUR_LEVEL_5_SCALE,
        6.0 + (blurriness - BLUR_STABLE * 12.0) / (BLUR_LEVEL_5_LIMIT - BLUR_STABLE * 12.0) * 5.0,
        scaleFactor};
  }
}

std::shared_ptr<ImageFilter> ImageFilter::Blur(float blurrinessX, float blurrinessY,
                                               TileMode tileMode) {
  if (blurrinessX < 0 || blurrinessY < 0 || (blurrinessX == 0 && blurrinessY == 0)) {
    return nullptr;
  }
  auto x = Get(blurrinessX);
  auto y = Get(blurrinessY);
  return std::make_shared<BlurImageFilter>(
      Point::Make(std::get<2>(x), std::get<2>(y)), std::max(std::get<1>(x), std::get<1>(y)),
      std::max(std::get<0>(x), std::get<0>(y)), tileMode, std::min(std::get<3>(x), std::get<3>(y)));
}

BlurImageFilter::BlurImageFilter(Point blurOffset, float downScaling, int iteration,
                                 TileMode tileMode, float scaleFactor)
    : blurOffset(blurOffset), downScaling(downScaling), iteration(iteration), tileMode(tileMode),
      scaleFactor(scaleFactor) {
}

void BlurImageFilter::draw(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags,
                           std::unique_ptr<FragmentProcessor> imageProcessor, float scaleFactor,
                           bool isDown) const {
  auto blurProcessor =
      DualBlurFragmentProcessor::Make(isDown ? DualBlurPassMode::Down : DualBlurPassMode::Up,
                                      std::move(imageProcessor), blurOffset * scaleFactor);
  auto drawingManager = renderTarget->getContext()->drawingManager();
  drawingManager->fillRTWithFP(std::move(renderTarget), std::move(blurProcessor), renderFlags);
}

Rect BlurImageFilter::onFilterBounds(const Rect& srcRect) const {
  auto mul = static_cast<float>(std::pow(2, iteration)) / (scaleFactor * downScaling);
  return srcRect.makeOutset(blurOffset.x * mul, blurOffset.y * mul);
}

std::shared_ptr<TextureProxy> BlurImageFilter::lockTextureProxy(std::shared_ptr<Image> source,
                                                                const Rect& clipBounds,
                                                                const TPArgs& args) const {
  if (FloatNearlyZero(scaleFactor)) {
    return nullptr;
  }
  auto isAlphaOnly = source->isAlphaOnly();
  auto lastRenderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(clipBounds.width()), static_cast<int>(clipBounds.height()),
      isAlphaOnly, 1, args.mipmapped);
  if (lastRenderTarget == nullptr) {
    return nullptr;
  }
  auto drawRect = Rect::MakeWH(lastRenderTarget->width(), lastRenderTarget->height());
  FPArgs fpArgs(args.context, args.renderFlags, drawRect);

  // Calculate the bounds of the filter after scaling.
  // `boundsWillSample` will determine the size of the texture after the first downsample.
  // By calculating the clip bound after filter, we can determine the bounds that will affect the
  // result. However, the bounds may be larger than the origin filter bounds, so we need to
  // intersect the bounds.
  auto boundsWillSample = filterBounds(clipBounds);
  auto filterOriginBounds = filterBounds(Rect::MakeWH(source->width(), source->height()));
  boundsWillSample.intersect(filterOriginBounds);

  // sampleOffset means the offset between the source bounds and the sample bounds.
  auto sampleOffset = Point::Make(boundsWillSample.left, boundsWillSample.top);
  boundsWillSample.scale(scaleFactor, scaleFactor);

  std::vector<std::shared_ptr<RenderTargetProxy>> renderTargets = {};
  // calculate the size of the source image of the first downsample
  auto textureSize = Size::Make(boundsWillSample.width(), boundsWillSample.height());
  // scale the source image to smaller size
  auto uvMatrix = Matrix::MakeScale(1 / (downScaling * scaleFactor));
  // calculate the uv matrix of the first downsample. Add sampleOffset to get the sample texture.
  uvMatrix.preTranslate(sampleOffset.x * scaleFactor * downScaling,
                        sampleOffset.y * scaleFactor * downScaling);
  auto sourceProcessor = FragmentProcessor::Make(source, fpArgs, tileMode, tileMode, {}, &uvMatrix);

  // downsample
  for (int i = 0; i < iteration; ++i) {
    renderTargets.push_back(lastRenderTarget);
    auto downWidth = static_cast<int>(roundf(textureSize.width * downScaling));
    auto downHeight = static_cast<int>(roundf(textureSize.height * downScaling));
    auto renderTarget =
        RenderTargetProxy::MakeFallback(args.context, downWidth, downHeight, isAlphaOnly);
    if (renderTarget == nullptr) {
      break;
    }

    if (sourceProcessor == nullptr) {
      uvMatrix = Matrix::MakeScale(textureSize.width / static_cast<float>(downWidth),
                                   textureSize.height / static_cast<float>(downHeight));

      sourceProcessor =
          TextureEffect::Make(lastRenderTarget->getTextureProxy(), {}, &uvMatrix, isAlphaOnly);
    }
    draw(renderTarget, args.renderFlags, std::move(sourceProcessor), downScaling, true);
    textureSize = Size::Make(static_cast<float>(downWidth), static_cast<float>(downHeight));
    lastRenderTarget = renderTarget;
  }

  // upsample
  auto upSampleScale = 1.0f / downScaling;
  for (size_t i = renderTargets.size(); i > 0; --i) {
    const auto& renderTarget = renderTargets[i - 1];
    if (i == 1) {
      // At the last iteration, we need to calculate the clip bounds of the filter.
      // Subtract sampleOffset means to convert sample bounds to origin bounds.
      // Add offset of clipBounds to apply clipBounds.
      uvMatrix = Matrix::MakeScale(textureSize.width / boundsWillSample.width(),
                                   textureSize.height / boundsWillSample.height());
      uvMatrix.preScale(scaleFactor, scaleFactor);
      uvMatrix.preTranslate(clipBounds.left - sampleOffset.x, clipBounds.top - sampleOffset.y);
      upSampleScale /= scaleFactor;
    } else {
      // at other iterations, we upscale only
      uvMatrix = Matrix::MakeScale(textureSize.width / static_cast<float>(renderTarget->width()),
                                   textureSize.height / static_cast<float>(renderTarget->height()));
    }
    sourceProcessor =
        TextureEffect::Make(lastRenderTarget->getTextureProxy(), {}, &uvMatrix, isAlphaOnly);
    draw(renderTarget, args.renderFlags, std::move(sourceProcessor), upSampleScale, false);
    lastRenderTarget = renderTarget;
    textureSize = Size::Make(static_cast<float>(renderTarget->width()),
                             static_cast<float>(renderTarget->height()));
  }
  return lastRenderTarget->getTextureProxy();
}

std::unique_ptr<FragmentProcessor> BlurImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  return makeFPFromTextureProxy(source, args, sampling, uvMatrix);
}
}  // namespace tgfx
