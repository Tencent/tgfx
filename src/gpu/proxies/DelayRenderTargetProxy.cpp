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

#include "DelayRenderTargetProxy.h"

namespace tgfx {
DelayRenderTargetProxy::DelayRenderTargetProxy(Context* context, int width, int height,
                                               PixelFormat format, ImageOrigin origin,
                                               std::shared_ptr<RenderTargetProvider> provider)
    : _context(context), _width(width), _height(height), _format(format), _origin(origin),
      _provider(std::move(provider)) {
}

Context* DelayRenderTargetProxy::getContext() const {
  return _context;
}

int DelayRenderTargetProxy::width() const {
  return _width;
}

int DelayRenderTargetProxy::height() const {
  return _height;
}

PixelFormat DelayRenderTargetProxy::format() const {
  return _format;
}

int DelayRenderTargetProxy::sampleCount() const {
  return 1;
}

ImageOrigin DelayRenderTargetProxy::origin() const {
  return _origin;
}

bool DelayRenderTargetProxy::externallyOwned() const {
  return true;
}

std::shared_ptr<TextureView> DelayRenderTargetProxy::getTextureView() const {
  return nullptr;
}

std::shared_ptr<RenderTarget> DelayRenderTargetProxy::getRenderTarget() const {
  if (_renderTarget == nullptr && _provider != nullptr) {
    _renderTarget = _provider->getRenderTarget(_context);
  }
  return _renderTarget;
}

void DelayRenderTargetProxy::reset() {
  _renderTarget = nullptr;
}
}  // namespace tgfx
