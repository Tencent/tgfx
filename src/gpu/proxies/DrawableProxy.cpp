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

#include "DrawableProxy.h"
#include "tgfx/gpu/Drawable.h"

namespace tgfx {
DrawableProxy::DrawableProxy(Context* context, Drawable* drawable)
    : _context(context), _drawable(drawable) {
}

Context* DrawableProxy::getContext() const {
  return _context;
}

int DrawableProxy::width() const {
  return _drawable->width();
}

int DrawableProxy::height() const {
  return _drawable->height();
}

PixelFormat DrawableProxy::format() const {
  return _drawable->onGetPixelFormat();
}

int DrawableProxy::sampleCount() const {
  return _drawable->onGetSampleCount();
}

ImageOrigin DrawableProxy::origin() const {
  return _drawable->onGetOrigin();
}

bool DrawableProxy::externallyOwned() const {
  return true;
}

std::shared_ptr<TextureView> DrawableProxy::getTextureView() const {
  return nullptr;
}

std::shared_ptr<RenderTarget> DrawableProxy::getRenderTarget() const {
  if (_renderTarget == nullptr) {
    _renderTarget = _drawable->onCreateRenderTarget(_context);
  }
  return _renderTarget;
}

Drawable* DrawableProxy::getDrawable() const {
  return _drawable;
}
}  // namespace tgfx
