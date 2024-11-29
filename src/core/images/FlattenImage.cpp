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

#include "FlattenImage.h"
#include "gpu/OpContext.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
std::shared_ptr<Image> FlattenImage::MakeFrom(std::shared_ptr<Image> source) {
  TRACE_EVENT;
  if (source == nullptr) {
    return nullptr;
  }
  auto mipmapped = source->hasMipmaps();
  if (mipmapped) {
    auto newSource = source->makeMipmapped(false);
    if (newSource != nullptr) {
      source = std::move(newSource);
    }
  }
  auto flattenImage =
      std::shared_ptr<FlattenImage>(new FlattenImage(UniqueKey::Make(), std::move(source)));
  flattenImage->weakThis = flattenImage;
  return mipmapped ? flattenImage->makeMipmapped(true) : flattenImage;
}

FlattenImage::FlattenImage(UniqueKey uniqueKey, std::shared_ptr<Image> source)
    : ResourceImage(std::move(uniqueKey)), source(std::move(source)) {
}

int FlattenImage::width() const {
  return source->width();
}

int FlattenImage::height() const {
  return source->height();
}

std::shared_ptr<Image> FlattenImage::onMakeDecoded(Context* context, bool) const {
  TRACE_EVENT;
  // There is no need to pass tryHardware (disabled) to the source image, as our texture proxy is
  // not locked from the source image.
  auto newSource = source->onMakeDecoded(context);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<FlattenImage>(new FlattenImage(uniqueKey, std::move(newSource)));
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<TextureProxy> FlattenImage::onLockTextureProxy(const TPArgs& args,
                                                               const UniqueKey& key) const {
  TRACE_EVENT;
  auto proxyProvider = args.context->proxyProvider();
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(key);
  if (textureProxy != nullptr) {
    return textureProxy;
  }
  auto sourceArgs = args;
  sourceArgs.renderFlags |= RenderFlags::DisableCache;
  textureProxy = source->lockTextureProxy(sourceArgs);
  if (textureProxy && textureProxy->isYUV()) {
    auto renderTarget = RenderTargetProxy::MakeFallback(args.context, width(), height(),
                                                        isAlphaOnly(), 1, args.mipmapped);
    if (renderTarget == nullptr) {
      return nullptr;
    }
    auto processor = TextureEffect::Make(std::move(textureProxy));
    if (processor == nullptr) {
      return nullptr;
    }
    OpContext opContext(renderTarget, args.renderFlags);
    opContext.fillWithFP(std::move(processor), Matrix::I(), true);
    textureProxy = renderTarget->getTextureProxy();
  }
  proxyProvider->changeUniqueKey(textureProxy, key, args.renderFlags);
  return textureProxy;
}

}  // namespace tgfx
