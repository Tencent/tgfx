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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
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

void Drawable::onAbandon() {
}

void Drawable::abandon() {
  if (_delivery == Delivery::Presented) {
    return;
  }
  onAbandon();
  releaseFrameHandles();
}

std::shared_ptr<RenderTargetProxy> Drawable::import(Context* context) {
  auto window = _window.lock();
  if (context == nullptr || window == nullptr) {
    return nullptr;
  }
  if (context->device() != window->getDevice().get()) {
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

bool Drawable::isFrameOpen() const {
  return _delivery == Delivery::Imported || _delivery == Delivery::Submitted;
}

bool Drawable::canReadBack() const {
  // The frame must still be valid: presented frames are terminal states, a presentation has
  // already been registered for PresentRequested frames (later readbacks would be ordered after
  // the presentation), and releaseFrameHandles() has already cleared _window for them, so check
  // the delivery state before dereferencing _window.
  if (_delivery != Delivery::Imported && _delivery != Delivery::Submitted) {
    return false;
  }
  // The frame is readable only when its source window also supports readback: blitting from a
  // framebufferOnly Metal layer (or a swapchain without copy-source usage) would trigger GPU
  // validation assertions.
  auto window = _window.lock();
  if (window == nullptr) {
    return false;
  }
  return window->supportsReadback();
}

bool Drawable::requestPresent(Context* context) {
  if (context == nullptr) {
    return false;
  }
  // The window may already be gone: frames hold it weakly (to break the device reference
  // cycle), and frame-level backends present through their proxies without it. The device
  // was validated when the frame was imported; skip the re-validation once the window is gone.
  if (auto window = _window.lock()) {
    if (context->device() != window->getDevice().get()) {
      LOGE("Drawable::requestPresent() The context must belong to the frame's window device!");
      return false;
    }
  }
  if (_delivery == Delivery::Imported || _delivery == Delivery::Submitted) {
    // Close the frame for further drawing; the caller records the presentation request on the
    // current recording batch, so it is delivered by the submission of that batch — ordered
    // after the frame's rendering commands and any still-pending work of earlier batches.
    _delivery = Delivery::PresentRequested;
    return true;
  }
  LOGE("Drawable::requestPresent() The frame is not in a presentable state!");
  return false;
}

void Drawable::markSubmitted() {
  if (_delivery == Delivery::Imported) {
    _delivery = Delivery::Submitted;
  }
}

void Drawable::markPresented() {
  if (_delivery == Delivery::PresentRequested) {
    _delivery = Delivery::Presented;
    releaseFrameHandles();
  }
}

void Drawable::releaseFrameHandles() {
  _window = {};
  _importedTarget = nullptr;
}

}  // namespace tgfx
