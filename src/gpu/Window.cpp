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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/gpu/Window.h"
#include "core/utils/Log.h"
#include "gpu/ProxyDrawable.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
Window::Window(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace,
               bool vsyncEnabled)
    : _device(std::move(device)), _colorSpace(std::move(colorSpace)), _vsyncEnabled(vsyncEnabled) {
}

std::shared_ptr<ColorSpace> Window::colorSpace() const {
  return _colorSpace;
}

bool Window::vsyncEnabled() const {
  return _vsyncEnabled;
}

std::shared_ptr<Device> Window::getDevice() {
  std::lock_guard<std::mutex> autoLock(locker);
  return _device;
}

std::shared_ptr<Drawable> Window::nextDrawable() {
  if (weak_from_this().expired()) {
    LOGE("Window::nextDrawable() The window must be owned by a shared_ptr!");
    return nullptr;
  }
  auto drawable = onNextDrawable();
  if (drawable != nullptr) {
    // The frame holds its window only weakly (to break the device reference cycle); the
    // Context's unsubmitted drawing buffers keep the window alive while the frame is
    // undelivered. Device validation happens later, when the frame is imported into a Context.
    drawable->_window = shared_from_this();
  }
  return drawable;
}

std::shared_ptr<Drawable> Window::onNextDrawable() {
  return ProxyDrawable::Make(shared_from_this());
}

std::shared_ptr<RenderTargetProxy> Window::getAutomaticTarget(Context* context) {
  std::lock_guard<std::mutex> autoLock(locker);
  auto iter = _automaticTargets.find(context);
  if (iter != _automaticTargets.end()) {
    if (auto target = iter->second.lock()) {
      return target;
    }
    // The Context released its surfaces; create a fresh target for it below.
    _automaticTargets.erase(iter);
  }
  auto target = onCreateRenderTarget(context);
  if (target != nullptr) {
    _automaticTargets[context] = target;
  }
  return target;
}

void Window::onPresent(Context*) {
}
}  // namespace tgfx
