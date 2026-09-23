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
#include "gpu/WindowDrawable.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
Window::Window(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace,
               bool vsyncEnabled)
    : device(std::move(device)), _colorSpace(std::move(colorSpace)), _vsyncEnabled(vsyncEnabled) {
}

std::shared_ptr<ColorSpace> Window::colorSpace() const {
  return _colorSpace;
}

bool Window::vsyncEnabled() const {
  return _vsyncEnabled;
}

std::shared_ptr<Device> Window::getDevice() {
  std::lock_guard<std::mutex> autoLock(locker);
  return device;
}

std::shared_ptr<Drawable> Window::nextDrawable(Context* context) {
  if (context == nullptr) {
    return nullptr;
  }
  if (context->device() != device.get()) {
    LOGE("Window::nextDrawable() The context must belong to the window's device!");
    return nullptr;
  }
  if (weak_from_this().expired()) {
    LOGE("Window::nextDrawable() The window must be owned by a shared_ptr!");
    return nullptr;
  }
  auto window = shared_from_this();
  auto drawable = onNextDrawable(context);
  if (drawable != nullptr) {
    drawable->_window = std::move(window);
  }
  return drawable;
}

std::shared_ptr<Drawable> Window::onNextDrawable(Context* context) {
  return WindowDrawable::Make(context, shared_from_this());
}

void Window::onPresent(Context*, const std::vector<std::shared_ptr<RenderTargetProxy>>&) {
}

bool Window::hasIndependentPresentationTargets() const {
  return false;
}
}  // namespace tgfx
