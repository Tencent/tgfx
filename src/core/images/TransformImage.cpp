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

#include "TransformImage.h"
#include "core/utils/AddressOf.h"
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
TransformImage::TransformImage(std::shared_ptr<Image> source) : source(std::move(source)) {
}

std::shared_ptr<TextureProxy> TransformImage::lockTextureProxy(const TPArgs& args) const {
  return lockTextureProxySubset(args, Rect::MakeWH(width(), height()));
}

std::shared_ptr<TextureProxy> TransformImage::lockTextureProxySubset(
    const TPArgs& args, const Rect& drawRect, const SamplingOptions& samplingOptions) const {
  auto rect = drawRect;
  if (args.drawScale < 1.0) {
    rect.scale(args.drawScale, args.drawScale);
  }
  rect.round();
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(rect.width()), static_cast<int>(rect.height()),
      alphaRenderable && isAlphaOnly(), 1, args.mipmapped, ImageOrigin::TopLeft,
      args.backingFit);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto uvMatrix =
      Matrix::MakeScale(drawRect.width() / rect.width(), drawRect.height() / rect.height());
  uvMatrix.postTranslate(drawRect.left, drawRect.top);
  auto sourceMatrix = concatUVMatrix(&uvMatrix);
  FPArgs fpArgs(args.context, args.renderFlags, Rect::MakeWH(rect.width(), rect.height()),
                1.0f / sourceMatrix->getMinScale());
  auto processor = FragmentProcessor::Make(source, fpArgs, samplingOptions, SrcRectConstraint::Fast,
                                           AddressOf(sourceMatrix));
  auto drawingManager = args.context->drawingManager();
  if (!drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags)) {
    return nullptr;
  }
  return renderTarget->asTextureProxy();
}

std::shared_ptr<Image> TransformImage::onMakeDecoded(Context* context, bool tryHardware) const {
  auto newSource = source->onMakeDecoded(context, tryHardware);
  if (newSource == nullptr) {
    return nullptr;
  }
  return onCloneWith(std::move(newSource));
}

std::shared_ptr<Image> TransformImage::onMakeMipmapped(bool enabled) const {
  auto newSource = source->makeMipmapped(enabled);
  if (newSource == nullptr) {
    return nullptr;
  }
  return onCloneWith(std::move(newSource));
}
}  // namespace tgfx