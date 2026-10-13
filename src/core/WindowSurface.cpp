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
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {

std::shared_ptr<Surface> WindowSurface::Make(Context* context, std::shared_ptr<Window> window,
                                             uint32_t renderFlags) {
  if (context == nullptr || window == nullptr) {
    return nullptr;
  }
  // All surfaces of this window share one automatic target within the current recording batch
  // (unified frame acquisition): one frame identity per window per batch, presented once at the
  // end of the batch. The batch cache is cleared at flush time, so a surface created for a new
  // batch (for example after a window resize) resolves a fresh target.
  auto proxy = context->drawingManager()->getAutomaticTarget(window);
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
    // Drawing into the window frame: the batch uses the frame and presents it once at the end
    // of the batch.
    drawingManager->collectFrameUsage(renderTarget);
    drawingManager->collectWindow(_window, renderTarget);
  }
}

void WindowSurface::onCollectFrameUsage(DrawingManager* drawingManager,
                                        const std::shared_ptr<RenderTargetProxy>& renderTarget) {
  if (drawingManager != nullptr && renderTarget != nullptr) {
    // Reading the window frame (a readback or snapshot copy) only uses it; the presentation
    // stays with the batches that draw into it.
    drawingManager->collectFrameUsage(renderTarget);
  }
}

bool WindowSurface::onValidateReadback(
    const std::shared_ptr<RenderTargetProxy>& renderTarget) const {
  if (_window == nullptr || !_window->supportsReadback() || renderTarget == nullptr) {
    // The window's frame buffer cannot act as a copy source.
    return false;
  }
  // The frame is only readable within the batch that draws it: the readback transfer then runs
  // in the submission that presents the frame, ordered after its drawing and before the
  // presentation. Once that batch has been flushed, its frame is handed to the display and a
  // readback would resolve a different frame.
  auto drawingManager = renderTarget->getContext()->drawingManager();
  return drawingManager->hasPendingPresentation(renderTarget);
}

}  // namespace tgfx
