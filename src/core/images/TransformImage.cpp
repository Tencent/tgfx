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
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
TransformImage::TransformImage(std::shared_ptr<Image> source) : source(std::move(source)) {
}

std::shared_ptr<TextureProxy> TransformImage::lockTextureProxy(const TPArgs& args) const {
  auto textureWidth = width();
  auto textureHeight = height();
  if (textureWidth < args.width && textureHeight < args.height) {
    textureWidth = args.width;
    textureHeight = args.height;
  }
  auto renderTarget =
      RenderTargetProxy::MakeFallback(args.context, textureWidth, textureHeight, isAlphaOnly(), 1,
                                      args.mipmapped, ImageOrigin::TopLeft, args.backingFit);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto textureScaleX = static_cast<float>(textureWidth) / static_cast<float>(width());
  auto textureScaleY = static_cast<float>(textureHeight) / static_cast<float>(height());
  auto uvMatrix = Matrix::MakeScale(1.0f / textureScaleX, 1.0f / textureScaleY);
  auto drawRect = Rect::MakeWH(textureWidth, textureHeight);
  FPArgs fpArgs(args.context, args.renderFlags, drawRect, std::max(textureScaleX, textureScaleY));
  SamplingArgs samplingArgs = {TileMode::Clamp, TileMode::Clamp, {}, SrcRectConstraint::Fast};
  auto processor = asFragmentProcessor(fpArgs, samplingArgs, &uvMatrix);
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