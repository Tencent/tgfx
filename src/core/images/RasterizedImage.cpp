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

#include "RasterizedImage.h"
#include "core/images/SubsetImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/DrawOp.h"

namespace tgfx {

std::shared_ptr<Image> RasterizedImage::MakeFrom(std::shared_ptr<Image> source) {
  if (source == nullptr) {
    return nullptr;
  }
  auto result =
      std::shared_ptr<RasterizedImage>(new RasterizedImage(UniqueKey::Make(), std::move(source)));
  result->weakThis = result;
  return result;
}

RasterizedImage::RasterizedImage(UniqueKey uniqueKey, std::shared_ptr<Image> source)
    : ResourceImage(std::move(uniqueKey)), source(std::move(source)) {
}

std::shared_ptr<TextureProxy> RasterizedImage::onLockTextureProxy(const TPArgs& args,
                                                                  const UniqueKey& key) const {
  auto proxyProvider = args.context->proxyProvider();
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(key);
  if (textureProxy != nullptr) {
    return textureProxy;
  }
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format = isAlphaOnly() && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  auto renderTarget = proxyProvider->createRenderTargetProxy(key, width(), height(), format, 1,
                                                             args.mipmapped, ImageOrigin::TopLeft,
                                                             BackingFit::Exact, args.renderFlags);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto drawRect = Rect::MakeWH(width(), height());
  FPArgs fpArgs(args.context, args.renderFlags, drawRect);
  auto processor = FragmentProcessor::Make(source, fpArgs, {}, SrcRectConstraint::Fast);
  if (processor == nullptr) {
    return nullptr;
  }
  auto drawingManager = renderTarget->getContext()->drawingManager();
  drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags);
  return renderTarget->asTextureProxy();
}
}  // namespace tgfx
