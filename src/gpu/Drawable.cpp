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
#include "core/utils/Log.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {

Drawable::Drawable(int width, int height, std::shared_ptr<ColorSpace> colorSpace)
    : _width(width), _height(height), _colorSpace(std::move(colorSpace)) {
}

void Drawable::onAttachSubmission(Context*) {
}

bool Drawable::onSchedulePresent(Context*) {
  return false;
}

void Drawable::onAbandon() {
}

void Drawable::abandon() {
  if (_delivery == Delivery::Presented || _delivery == Delivery::Abandoned) {
    return;
  }
  onAbandon();
  _delivery = Delivery::Abandoned;
  releaseFrameHandles();
}

void Drawable::markPresentationRequested() {
  if (_delivery == Delivery::Imported || _delivery == Delivery::Acquired) {
    _delivery = Delivery::PresentRequested;
  }
}

std::shared_ptr<RenderTargetProxy> Drawable::import(Context* context) {
  if (context == nullptr || _window == nullptr) {
    return nullptr;
  }
  if (context->device() != _window->getDevice().get()) {
    LOGE("Drawable::import() The context must belong to the frame's window device!");
    return nullptr;
  }
  if (_delivery != Delivery::Acquired) {
    LOGE("Drawable::import() The frame has already been imported!");
    return nullptr;
  }
  auto target = onImport(context);
  if (target == nullptr) {
    return nullptr;
  }
  _importedTarget = target;
  // Deferred backends do not know the frame size at acquisition time; resolve it from the
  // imported target.
  if (_width == 0 || _height == 0) {
    _width = target->width();
    _height = target->height();
  }
  _delivery = Delivery::Imported;
  return target;
}

bool Drawable::canReadBack() const {
  // The frame must still be valid: presented or abandoned frames are terminal states, a
  // presentation has already been registered for PresentRequested frames (later readbacks would
  // be ordered after the presentation), and releaseFrameHandles() has already cleared _window
  // for them, so check the delivery state before dereferencing _window.
  if (_delivery != Delivery::Imported && _delivery != Delivery::Submitted) {
    return false;
  }
  // The frame is readable only when its source window also supports readback: blitting from a
  // framebufferOnly Metal layer (or a swapchain without copy-source usage) would trigger GPU
  // validation assertions.
  if (_window == nullptr) {
    return false;
  }
  return _window->onSupportsReadback();
}

bool Drawable::requestPresent(Context* context) {
  if (context == nullptr || _window == nullptr) {
    return false;
  }
  if (context->device() != _window->getDevice().get()) {
    LOGE("Drawable::requestPresent() The context must belong to the frame's window device!");
    return false;
  }
  if (_delivery == Delivery::Imported) {
    // The frame's rendering has not been submitted yet: register the request on this frame. The
    // request rides along with the submission of the drawing buffer that carries the frame's
    // rendering commands (see scheduleIfRequested()), so the presentation is never consumed by
    // an unrelated earlier submission.
    _delivery = Delivery::PresentRequested;
    return true;
  }
  if (_delivery == Delivery::Submitted) {
    // The rendering has been submitted (for example a readback was scheduled in between): flush
    // and submit any pending work first — readback transfers scheduled after the frame's
    // rendering submission are still unsubmitted at this point and must be submitted before the
    // presentation, otherwise they would execute on an already-presented frame. This is a no-op
    // when there is nothing pending. Then present immediately, ordered after all submitted work.
    context->flushAndSubmit();
    onPresent(context);
    _delivery = Delivery::Presented;
    releaseFrameHandles();
    return true;
  }
  LOGE("Drawable::requestPresent() The frame is not in a presentable state!");
  return false;
}

void Drawable::scheduleIfRequested(Context* context) {
  // Wire every undelivered frame into the submission of the drawing buffer that carries its
  // rendering commands, so the wiring (and any registered presentation) is ordered with that
  // submission and never consumed by an unrelated earlier submission.
  if (_delivery == Delivery::PresentRequested) {
    onAttachSubmission(context);
    _presentationAttached = onSchedulePresent(context);
  } else if (_delivery == Delivery::Imported) {
    // The rendering is submitted without a registered presentation (Context::present() may
    // still come later, on the Submitted path); backends that need submission-time
    // synchronization with the frame acquisition still wire it here.
    onAttachSubmission(context);
  }
}

void Drawable::onSubmissionCompleted(Context* context) {
  if (_delivery == Delivery::Imported) {
    _delivery = Delivery::Submitted;
    return;
  }
  if (_delivery == Delivery::PresentRequested) {
    if (!_presentationAttached) {
      onPresent(context);
    }
    _delivery = Delivery::Presented;
    releaseFrameHandles();
  }
}

void Drawable::releaseFrameHandles() {
  _window = nullptr;
  _importedTarget = nullptr;
}

}  // namespace tgfx
