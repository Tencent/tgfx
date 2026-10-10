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

#include "core/DrawableSurface.h"
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/Drawable.h"

namespace tgfx {

std::shared_ptr<Surface> DrawableSurface::Make(Context* context, std::shared_ptr<Drawable> drawable,
                                               uint32_t renderFlags) {
  if (context == nullptr || drawable == nullptr) {
    return nullptr;
  }
  // Import the frame into this Context, which also validates that the context belongs to the
  // frame's window device and that the frame has not been imported before.
  auto renderTarget = drawable->import(context);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto surface = std::shared_ptr<DrawableSurface>(new DrawableSurface(
      std::move(renderTarget), renderFlags, true, drawable->colorSpace(), std::move(drawable)));
  // Track the first frame here (the RenderContext constructor cannot dispatch to this
  // subclass while the base class is still being constructed).
  surface->onCollectFrame(context->drawingManager(), nullptr);
  return surface;
}

DrawableSurface::DrawableSurface(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                                 bool clearAll, std::shared_ptr<ColorSpace> colorSpace,
                                 std::shared_ptr<Drawable> drawable)
    : Surface(std::move(proxy), renderFlags, clearAll, std::move(colorSpace)),
      _drawable(std::move(drawable)) {
}

void DrawableSurface::onCollectFrame(DrawingManager* drawingManager,
                                     const std::shared_ptr<RenderTargetProxy>&) {
  if (drawingManager != nullptr && _drawable != nullptr) {
    // The drawable's frame must be tracked by the current drawing buffer, so that its delivery
    // state advances when the submission carrying the frame's rendering commands completes.
    drawingManager->collectDrawable(_drawable);
  }
}

bool DrawableSurface::onValidateDraw() const {
  // A presentation has been registered for (or delivered to) this frame: the frame is closed
  // and must not be drawn into anymore.
  return _drawable == nullptr || _drawable->canReadBack();
}

bool DrawableSurface::onValidateReadback() const {
  // Readback is defined until a presentation is registered and only if the frame's window
  // supports readback.
  return _drawable != nullptr && _drawable->canReadBack();
}

}  // namespace tgfx
