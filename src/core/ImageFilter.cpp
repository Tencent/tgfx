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
                                                            const Rect& clipBounds,
                                                            const TPArgs& args,
                                                            Point* textureScales) const {

  auto scaledBounds = clipBounds;
  scaledBounds.scale(args.drawScales.x, args.drawScales.y);
  scaledBounds.roundOut();

  auto actualScales = Point::Make(scaledBounds.width() / clipBounds.width(),
                                  scaledBounds.height() / clipBounds.height());

  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(scaledBounds.width()), static_cast<int>(scaledBounds.height()),
      source->isAlphaOnly(), 1, args.mipmapped, ImageOrigin::TopLeft, BackingFit::Approx);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  FPArgs fpArgs(args.context, args.renderFlags,
                Rect::MakeWH(renderTarget->width(), renderTarget->height()), actualScales);
  Matrix matrix = Matrix::MakeTrans(clipBounds.left, clipBounds.top);
  matrix.preScale(1.0f / actualScales.x, 1.0f / actualScales.y);
  auto processor = asFragmentProcessor(std::move(source), fpArgs, args.scaleSampling,
                                       SrcRectConstraint::Fast, &matrix);
  auto drawingManager = args.context->drawingManager();
  if (!drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags)) {
    return nullptr;
  }
  if (textureScales) {
    *textureScales = actualScales;
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
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped, args.drawScales, sampling);
  Point textureScales = Point::Make(1.0f, 1.0f);
  auto textureProxy = lockTextureProxy(std::move(source), dstBounds, tpArgs, &textureScales);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto fpMatrix = Matrix::MakeTrans(-dstBounds.x(), -dstBounds.y());
  fpMatrix.postScale(textureScales.x, textureScales.y);
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
