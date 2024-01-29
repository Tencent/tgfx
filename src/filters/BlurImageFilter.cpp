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
#include "gpu/RenderContext.h"
#include "gpu/TextureSampler.h"
#include "gpu/processors/DualBlurFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
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

static std::tuple<int, float, float> Get(float blurriness) {
  blurriness = blurriness < BLUR_LEVEL_MAX_LIMIT ? blurriness : BLUR_LEVEL_MAX_LIMIT;
  if (blurriness < BLUR_LEVEL_1_LIMIT) {
    return {BLUR_LEVEL_1_DEPTH, BLUR_LEVEL_1_SCALE, blurriness / BLUR_LEVEL_1_LIMIT * 2.0};
  } else if (blurriness < BLUR_LEVEL_2_LIMIT) {
    return {BLUR_LEVEL_2_DEPTH, BLUR_LEVEL_2_SCALE,
            (blurriness - BLUR_STABLE) / (BLUR_LEVEL_2_LIMIT - BLUR_STABLE) * 3.0};
  } else if (blurriness < BLUR_LEVEL_3_LIMIT) {
    return {BLUR_LEVEL_3_DEPTH, BLUR_LEVEL_3_SCALE,
            (blurriness - BLUR_STABLE) / (BLUR_LEVEL_3_LIMIT - BLUR_STABLE) * 5.0};
  } else if (blurriness < BLUR_LEVEL_4_LIMIT) {
    return {BLUR_LEVEL_4_DEPTH, BLUR_LEVEL_4_SCALE,
            (blurriness - BLUR_STABLE) / (BLUR_LEVEL_4_LIMIT - BLUR_STABLE) * 6.0};
  } else {
    return {
        BLUR_LEVEL_5_DEPTH, BLUR_LEVEL_5_SCALE,
        6.0 + (blurriness - BLUR_STABLE * 12.0) / (BLUR_LEVEL_5_LIMIT - BLUR_STABLE * 12.0) * 5.0};
  }
}

std::shared_ptr<ImageFilter> ImageFilter::Blur(float blurrinessX, float blurrinessY,
                                               TileMode tileMode, const Rect* cropRect) {
  if (blurrinessX < 0 || blurrinessY < 0 || (blurrinessX == 0 && blurrinessY == 0) ||
      (cropRect && cropRect->isEmpty())) {
    return nullptr;
  }
  auto x = Get(blurrinessX);
  auto y = Get(blurrinessY);
  return std::make_shared<BlurImageFilter>(
      Point::Make(std::get<2>(x), std::get<2>(y)), std::max(std::get<1>(x), std::get<1>(y)),
      std::max(std::get<0>(x), std::get<0>(y)), tileMode, cropRect);
}

BlurImageFilter::BlurImageFilter(Point blurOffset, float downScaling, int iteration,
                                 TileMode tileMode, const Rect* cropRect)
    : ImageFilter(cropRect), blurOffset(blurOffset), downScaling(downScaling), iteration(iteration),
      tileMode(tileMode) {
}

void BlurImageFilter::draw(std::shared_ptr<RenderTargetProxy> renderTarget,
                           std::unique_ptr<FragmentProcessor> imageProcessor,
                           const Rect& imageBounds, bool isDown) const {
  auto dstWidth = static_cast<float>(renderTarget->width());
  auto dstHeight = static_cast<float>(renderTarget->height());
  auto localMatrix =
      Matrix::MakeScale(imageBounds.width() / dstWidth, imageBounds.height() / dstHeight);
  localMatrix.postTranslate(imageBounds.x(), imageBounds.y());
  auto texelSize = Size::Make(0.5f / imageBounds.width(), 0.5f / imageBounds.height());
  auto blurProcessor =
      DualBlurFragmentProcessor::Make(isDown ? DualBlurPassMode::Down : DualBlurPassMode::Up,
                                      std::move(imageProcessor), blurOffset, texelSize);
  RenderContext renderContext(std::move(renderTarget));
  renderContext.fillWithFP(std::move(blurProcessor), localMatrix, true);
}

Rect BlurImageFilter::onFilterBounds(const Rect& srcRect) const {
  auto mul = static_cast<float>(std::pow(2, iteration)) / downScaling;
  return srcRect.makeOutset(blurOffset.x * mul, blurOffset.y * mul);
}

std::unique_ptr<FragmentProcessor> BlurImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const ImageFPArgs& args, const Matrix* localMatrix,
    const Rect* clipBounds) const {
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  Rect dstBounds = Rect::MakeEmpty();
  if (!applyCropRect(inputBounds, &dstBounds, clipBounds)) {
    return nullptr;
  }
  ImageFPArgs imageArgs(args.context, {}, args.renderFlags, tileMode, tileMode);
  auto processor = FragmentProcessor::MakeFromImage(source, imageArgs, nullptr, &dstBounds);
  auto imageBounds = dstBounds;
  std::vector<std::shared_ptr<RenderTargetProxy>> renderTargets = {};
  auto mipMapped = source->hasMipmaps() && args.sampling.mipMapMode != MipMapMode::None;
  auto lastRenderTarget = RenderTargetProxy::Make(
      args.context, static_cast<int>(imageBounds.width()), static_cast<int>(imageBounds.height()),
      PixelFormat::RGBA_8888, 1, mipMapped);
  if (lastRenderTarget == nullptr) {
    return nullptr;
  }
  for (int i = 0; i < iteration; ++i) {
    renderTargets.push_back(lastRenderTarget);
    if (processor == nullptr) {
      processor = TextureEffect::Make(lastRenderTarget->getTextureProxy());
    }
    int downWidth = static_cast<int>(imageBounds.width() * downScaling);
    int downHeight = static_cast<int>(imageBounds.height() * downScaling);
    auto renderTarget = RenderTargetProxy::Make(args.context, downWidth, downHeight);
    if (renderTarget == nullptr) {
      return nullptr;
    }
    draw(renderTarget, std::move(processor), imageBounds, true);
    lastRenderTarget = renderTarget;
    imageBounds = Rect::MakeWH(downWidth, downHeight);
  }
  for (size_t i = renderTargets.size(); i > 0; --i) {
    processor = TextureEffect::Make(lastRenderTarget->getTextureProxy());
    auto renderTarget = renderTargets[i - 1];
    draw(renderTarget, std::move(processor), imageBounds, false);
    lastRenderTarget = renderTarget;
    imageBounds = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  }
  auto matrix = Matrix::MakeTrans(-dstBounds.x(), -dstBounds.y());
  if (localMatrix != nullptr) {
    matrix.preConcat(*localMatrix);
  }
  return TiledTextureEffect::Make(lastRenderTarget->getTextureProxy(), args.tileModeX,
                                  args.tileModeY, args.sampling, &matrix);
}
}  // namespace tgfx
