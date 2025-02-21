/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "OffscreenImage.h"
#include "gpu/ProxyProvider.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {

OffscreenImage::OffscreenImage(UniqueKey uniqueKey) : ResourceImage(std::move(uniqueKey)) {
}

std::shared_ptr<TextureProxy> OffscreenImage::onLockTextureProxy(const TPArgs& args,
                                                                 const UniqueKey& key) const {
  auto proxyProvider = args.context->proxyProvider();
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(key);
  if (textureProxy != nullptr) {
    return textureProxy;
  }
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format = isAlphaOnly() && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  textureProxy = proxyProvider->createTextureProxy(key, width(), height(), format, args.mipmapped,
                                                   ImageOrigin::TopLeft, args.renderFlags);
  auto renderTarget = proxyProvider->createRenderTargetProxy(textureProxy, format);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto renderFlags = args.renderFlags | RenderFlags::DisableCache;
  if (!onDraw(renderTarget, renderFlags)) {
    return nullptr;
  }
  return textureProxy;
}
}  // namespace tgfx
