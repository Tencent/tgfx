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

namespace tgfx {
DrawableProxy::DrawableProxy(Context* context, int width, int height, PixelFormat format,
                             int sampleCount, ImageOrigin origin,
                             std::shared_ptr<RenderTarget> renderTarget)
    : _context(context), _width(width), _height(height), _format(format), _sampleCount(sampleCount),
      _origin(origin), _renderTarget(std::move(renderTarget)) {
}

Context* DrawableProxy::getContext() const {
  return _context;
}

int DrawableProxy::width() const {
  return _width;
}

int DrawableProxy::height() const {
  return _height;
}

PixelFormat DrawableProxy::format() const {
  return _format;
}

int DrawableProxy::sampleCount() const {
  return _sampleCount;
}

ImageOrigin DrawableProxy::origin() const {
  return _origin;
}

bool DrawableProxy::externallyOwned() const {
  return true;
}

std::shared_ptr<TextureView> DrawableProxy::getTextureView() const {
  return nullptr;
}

std::shared_ptr<RenderTarget> DrawableProxy::getRenderTarget() const {
  return _renderTarget;
}
}  // namespace tgfx
