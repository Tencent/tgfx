/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/ImageFilter.h"
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
Rect ImageFilter::filterBounds(const Rect& rect) const {
  Rect dstBounds = {};
  applyCropRect(rect, &dstBounds);
  return dstBounds;
}

Rect ImageFilter::onFilterBounds(const Rect& srcRect) const {
  return srcRect;
}

std::shared_ptr<TextureProxy> ImageFilter::lockTextureProxy(std::shared_ptr<Image> source,
                                                            const Rect& renderBounds,
                                                            const TPArgs& args) const {

  auto scaledBounds = renderBounds;
  if (args.drawScale < 1.0f) {
    scaledBounds.scale(args.drawScale, args.drawScale);
  }
  scaledBounds.roundOut();

  auto textureScaleX = scaledBounds.width() / renderBounds.width();
  auto textureScaleY = scaledBounds.height() / renderBounds.height();
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(scaledBounds.width()), static_cast<int>(scaledBounds.height()),
      source->isAlphaOnly(), 1, args.mipmapped, ImageOrigin::TopLeft,
      args.backingFit);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  FPArgs fpArgs(args.context, args.renderFlags,
                Rect::MakeWH(renderTarget->width(), renderTarget->height()),
                std::max(textureScaleX, textureScaleY));
  Matrix matrix = Matrix::MakeTrans(renderBounds.left, renderBounds.top);
  matrix.preScale(1.0f / textureScaleX, 1.0f / textureScaleY);
  auto processor = asFragmentProcessor(source, fpArgs, {}, SrcRectConstraint::Fast, &matrix);
  auto drawingManager = args.context->drawingManager();
  if (!drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags)) {
    return nullptr;
  }
  return renderTarget->asTextureProxy();
}

bool ImageFilter::applyCropRect(const Rect& srcRect, Rect* dstRect, const Rect* clipBounds) const {
  *dstRect = onFilterBounds(srcRect);
  if (clipBounds) {
    if (!dstRect->intersect(*clipBounds)) {
      return false;
    }
  }
  dstRect->roundOut();
  return true;
}

PlacementPtr<FragmentProcessor> ImageFilter::makeFPFromTextureProxy(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    const SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  auto clipBounds = args.drawRect;
  if (uvMatrix) {
    clipBounds = uvMatrix->mapRect(clipBounds);
  }
  Rect dstBounds = {};
  if (!applyCropRect(inputBounds, &dstBounds, &clipBounds)) {
    return nullptr;
  }
  auto isAlphaOnly = source->isAlphaOnly();
  auto mipmapped = source->hasMipmaps() && sampling.mipmapMode != MipmapMode::None;
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped, args.drawScale);
  auto textureProxy = lockTextureProxy(source, dstBounds, tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto fpMatrix = Matrix::MakeTrans(-dstBounds.x(), -dstBounds.y());
  fpMatrix.postScale(static_cast<float>(textureProxy->width()) / dstBounds.width(),
                     static_cast<float>(textureProxy->height()) / dstBounds.height());
  if (uvMatrix != nullptr) {
    fpMatrix.preConcat(*uvMatrix);
  }
  SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, sampling, constraint};
  if (dstBounds.contains(clipBounds)) {
    return TextureEffect::Make(std::move(textureProxy), samplingArgs, &fpMatrix, isAlphaOnly);
  }
  return TiledTextureEffect::Make(std::move(textureProxy), samplingArgs, &fpMatrix, isAlphaOnly);
}
}  // namespace tgfx
