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
  // The frame must still be valid: presented or abandoned frames are terminal states, and
  // releaseFrameHandles() has already cleared _window for them, so check the delivery state
  // before dereferencing _window.
  if (_delivery != Delivery::Imported && _delivery != Delivery::Submitted &&
      _delivery != Delivery::PresentRequested) {
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
    // The frame's rendering has not been submitted yet: register the request and let it ride
    // along with the submission that carries the frame's rendering commands.
    _delivery = Delivery::PresentRequested;
    _presentationAttached = onSchedulePresent(context);
    return true;
  }
  if (_delivery == Delivery::Submitted) {
    // The rendering has been submitted (for example a readback was scheduled in between):
    // present immediately, ordered after all previously submitted work.
    onPresent(context);
    _delivery = Delivery::Presented;
    releaseFrameHandles();
    return true;
  }
  LOGE("Drawable::requestPresent() The frame is not in a presentable state!");
  return false;
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
