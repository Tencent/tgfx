/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "QGLDrawableProxy.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
QGLDrawableProxy::QGLDrawableProxy(Context* context, int width, int height, PixelFormat format,
                                   int sampleCount, ImageOrigin origin)
    : DrawableProxy(context, width, height, format, sampleCount, origin, nullptr) {
}

bool QGLDrawableProxy::externallyOwned() const {
  return false;
}

void QGLDrawableProxy::ensureTextureRTProxy() const {
  if (textureRTProxy == nullptr) {
    textureRTProxy = RenderTargetProxy::Make(_context, _width, _height, false);
  }
}

std::shared_ptr<TextureView> QGLDrawableProxy::getTextureView() const {
  ensureTextureRTProxy();
  return textureRTProxy ? textureRTProxy->getTextureView() : nullptr;
}

std::shared_ptr<TextureProxy> QGLDrawableProxy::asTextureProxy() const {
  ensureTextureRTProxy();
  return textureRTProxy ? textureRTProxy->asTextureProxy() : nullptr;
}

std::shared_ptr<RenderTarget> QGLDrawableProxy::getRenderTarget() const {
  ensureTextureRTProxy();
  return textureRTProxy ? textureRTProxy->getRenderTarget() : nullptr;
}
}  // namespace tgfx
