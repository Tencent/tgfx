/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/gpu/Window.h"
#include "core/utils/Log.h"
#include "inspect/InspectorMark.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
Window::Window(std::shared_ptr<Device> device) : device(std::move(device)) {
}

std::shared_ptr<Device> Window::getDevice() {
  std::lock_guard<std::mutex> autoLock(locker);
  return device;
}

std::shared_ptr<tgfx::Surface> Window::getSurface(Context* context, bool queryOnly) {
  std::lock_guard<std::mutex> autoLock(locker);
  if (!checkContext(context)) {
    return nullptr;
  }
  if (surface != nullptr && !sizeInvalid) {
    return surface;
  }
  if (queryOnly) {
    return nullptr;
  }
  surface = onCreateSurface(context);
  sizeInvalid = false;
  return surface;
}

void Window::invalidSize() {
  std::lock_guard<std::mutex> autoLock(locker);
  sizeInvalid = true;
  onInvalidSize();
}

void Window::freeSurface() {
  std::lock_guard<std::mutex> autoLock(locker);
  onFreeSurface();
}

void Window::present(Context* context, int64_t presentationTime) {
  FRAME_MARK;
  std::lock_guard<std::mutex> autoLock(locker);
  if (!checkContext(context)) {
    return;
  }
  context->flushAndSubmit();
  onPresent(context, presentationTime);
}

void Window::onInvalidSize() {
}

void Window::onFreeSurface() {
  surface = nullptr;
}

bool Window::checkContext(Context* context) {
  if (context == nullptr) {
    return false;
  }
  if (context->device() != device.get()) {
    LOGE("Window::checkContext() : context is not locked from the same device of this window");
    return false;
  }
  return true;
}
}  // namespace tgfx