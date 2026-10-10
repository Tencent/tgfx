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

#include "core/WindowSurface.h"
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {

std::shared_ptr<Surface> WindowSurface::Make(Context* context, std::shared_ptr<Window> window,
                                             uint32_t renderFlags) {
  if (context == nullptr || window == nullptr) {
    return nullptr;
  }
  auto proxy = window->onCreateRenderTarget(context);
  if (proxy == nullptr) {
    return nullptr;
  }
  auto colorSpace = window->colorSpace();
  auto renderTarget = proxy;
  auto surface = std::shared_ptr<WindowSurface>(new WindowSurface(
      std::move(proxy), renderFlags, true, std::move(colorSpace), std::move(window)));
  // Collect the first frame here (the RenderContext constructor cannot dispatch to this
  // subclass while the base class is still being constructed).
  surface->onCollectFrame(context->drawingManager(), std::move(renderTarget));
  return surface;
}

WindowSurface::WindowSurface(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                             bool clearAll, std::shared_ptr<ColorSpace> colorSpace,
                             std::shared_ptr<Window> window)
    : Surface(std::move(proxy), renderFlags, clearAll, std::move(colorSpace)),
      _window(std::move(window)) {
}

void WindowSurface::onCollectFrame(DrawingManager* drawingManager,
                                   const std::shared_ptr<RenderTargetProxy>& renderTarget) {
  if (drawingManager != nullptr && _window != nullptr && renderTarget != nullptr) {
    drawingManager->collectWindow(_window, renderTarget);
  }
}

bool WindowSurface::onValidateReadback() const {
  // The automatic path is only readable when the window's frame buffer can act as a copy
  // source; the content is not defined once it has been submitted either way.
  return _window == nullptr || _window->supportsReadback();
}

}  // namespace tgfx
