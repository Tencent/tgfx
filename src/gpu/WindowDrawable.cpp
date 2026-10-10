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

#include "gpu/WindowDrawable.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

std::shared_ptr<WindowDrawable> WindowDrawable::Make(std::shared_ptr<Window> window) {
  if (window == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<WindowDrawable>(new WindowDrawable(window->colorSpace()));
}

WindowDrawable::WindowDrawable(std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(0, 0, std::move(colorSpace)) {
  // The frame size is not known until the frame is imported; Drawable::import() backfills it
  // from the resolved render target.
}

WindowDrawable::~WindowDrawable() {
  abandon();
}

std::shared_ptr<RenderTargetProxy> WindowDrawable::onImport(Context* context) {
  auto window = _window.lock();
  if (window == nullptr) {
    return nullptr;
  }
  auto renderTarget = window->onCreateRenderTarget(context);
  if (renderTarget != nullptr) {
    // Enable the default proxy-level presentation forwarding for shared-backbuffer backends.
    renderTarget->setPresentingWindow(_window);
  }
  return renderTarget;
}

void WindowDrawable::onPresent(Context* context) {
  if (_importedTarget == nullptr) {
    return;
  }
  _importedTarget->onPresentFrame(context);
}

}  // namespace tgfx
