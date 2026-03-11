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
#include "tgfx/gpu/Device.h"

namespace tgfx {
Window::Window(std::shared_ptr<Device> device) : device(std::move(device)) {
}

std::shared_ptr<Device> Window::getDevice() {
  std::lock_guard<std::mutex> autoLock(locker);
  return device;
}

std::shared_ptr<Drawable> Window::getDrawable(Context* context, bool queryOnly) {
  std::lock_guard<std::mutex> autoLock(locker);
  if (queryOnly) {
    return currentDrawable;
  }
  if (currentDrawable != nullptr && currentDrawable->isReusable()) {
    return currentDrawable;
  }
  currentDrawable = onCreateDrawable(context);
  return currentDrawable;
}

void Window::invalidSize() {
  std::lock_guard<std::mutex> autoLock(locker);
  currentDrawable = nullptr;
  onInvalidSize();
}

void Window::onInvalidSize() {
}
}  // namespace tgfx
