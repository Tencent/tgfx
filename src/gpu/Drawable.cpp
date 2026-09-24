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

#include "tgfx/gpu/Drawable.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Surface.h"

namespace tgfx {

Drawable::Drawable(Context* context, std::shared_ptr<RenderTargetProxy> renderTarget,
                   std::shared_ptr<ColorSpace> colorSpace)
    : _context(context), _renderTarget(std::move(renderTarget)),
      _colorSpace(std::move(colorSpace)) {
}

int Drawable::width() const {
  return _renderTarget->width();
}

int Drawable::height() const {
  return _renderTarget->height();
}

std::shared_ptr<ColorSpace> Drawable::colorSpace() const {
  return _colorSpace;
}

void Drawable::present() {
  if (_presented) {
    return;
  }
  _presented = true;
  onPresent();
}

bool Drawable::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX, int srcY) {
  if (_renderTarget == nullptr) {
    return false;
  }
  auto surface = _surface.lock();
  if (surface == nullptr) {
    // The Surface created from this Drawable has been released. Use a temporary Surface that
    // shares the render target, which is still valid because this Drawable holds it.
    surface = Surface::MakeFrom(_renderTarget, 0, false, _colorSpace);
    if (surface == nullptr) {
      return false;
    }
  }
  return surface->readPixels(dstInfo, dstPixels, srcX, srcY);
}

}  // namespace tgfx
