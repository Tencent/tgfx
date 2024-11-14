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

#include "tgfx/core/ImageFilter.h"
#include "core/utils/NeedMipmaps.h"
#include "gpu/DrawingManager.h"
#include "gpu/OpContext.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
Rect ImageFilter::filterBounds(const Rect& rect) const {
  auto dstBounds = Rect::MakeEmpty();
  applyCropRect(rect, &dstBounds);
  return dstBounds;
}

Rect ImageFilter::onFilterBounds(const Rect& srcRect) const {
  return srcRect;
}

std::shared_ptr<TextureProxy> ImageFilter::lockTextureProxy(std::shared_ptr<Image> source,
                                                            const Rect& clipBounds,
                                                            const TPArgs& args,
                                                            const SamplingOptions& sampling) const {
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(clipBounds.width()), static_cast<int>(clipBounds.height()),
      source->isAlphaOnly(), 1, args.mipmapped);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto drawRect = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  FPArgs fpArgs(args.context, args.renderFlags, drawRect, Matrix::I());
  auto offsetMatrix = Matrix::MakeTrans(clipBounds.x(), clipBounds.y());
  auto processor = asFragmentProcessor(std::move(source), fpArgs, sampling, &offsetMatrix);
  if (!processor) {
    return nullptr;
  }
  OpContext opContext(renderTarget, args.renderFlags);
  opContext.fillWithFP(std::move(processor), Matrix::I(), true);
  return renderTarget->getTextureProxy();
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

std::unique_ptr<FragmentProcessor> ImageFilter::makeFPFromTextureProxy(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  auto clipBounds = args.drawRect;
  if (uvMatrix) {
    clipBounds = uvMatrix->mapRect(clipBounds);
  }
  Rect dstBounds = Rect::MakeEmpty();
  if (!applyCropRect(inputBounds, &dstBounds, &clipBounds)) {
    return nullptr;
  }
  auto isAlphaOnly = source->isAlphaOnly();
  auto mipmapped = source->hasMipmaps() && NeedMipmaps(sampling, args.viewMatrix, uvMatrix);
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped);
  auto textureProxy = lockTextureProxy(std::move(source), dstBounds, tpArgs, sampling);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto fpMatrix = Matrix::MakeTrans(-dstBounds.x(), -dstBounds.y());
  if (uvMatrix != nullptr) {
    fpMatrix.preConcat(*uvMatrix);
  }
  return TextureEffect::Make(std::move(textureProxy), sampling, &fpMatrix, isAlphaOnly);
}
}  // namespace tgfx
