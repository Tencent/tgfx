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
#include "gpu/DrawingManager.h"
#include "gpu/OpContext.h"
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

std::shared_ptr<TextureProxy> ImageFilter::lockTextureProxy(Context* context,
                                                            std::shared_ptr<Image> source,
                                                            const Rect& filterBounds,
                                                            bool mipmapped,
                                                            uint32_t renderFlags) const {
  auto renderTarget = RenderTargetProxy::MakeFallback(
      context, static_cast<int>(filterBounds.width()), static_cast<int>(filterBounds.height()),
      source->isAlphaOnly(), 1, mipmapped);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto drawRect = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  FPArgs args(context, renderFlags, drawRect, Matrix::I());
  auto offsetMatrix = Matrix::MakeTrans(filterBounds.x(), filterBounds.y());
  // There is no scale, so we can use the default sampling options.
  auto processor = asFragmentProcessor(std::move(source), args, {}, &offsetMatrix);
  if (!processor) {
    return nullptr;
  }
  OpContext opContext(renderTarget, true);
  opContext.fillWithFP(std::move(processor), Matrix::I());
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
  auto mipmapped = source->hasMipmaps() && sampling.mipmapMode != MipmapMode::None;
  auto textureProxy =
      lockTextureProxy(args.context, source, dstBounds, mipmapped, args.renderFlags);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto fpMatrix = Matrix::MakeTrans(-dstBounds.x(), -dstBounds.y());
  if (uvMatrix != nullptr) {
    fpMatrix.preConcat(*uvMatrix);
  }
  return TextureEffect::Make(std::move(textureProxy), sampling, &fpMatrix);
}
}  // namespace tgfx
