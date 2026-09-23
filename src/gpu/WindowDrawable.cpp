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

#include "gpu/WindowDrawable.h"

namespace tgfx {

std::shared_ptr<WindowDrawable> WindowDrawable::Make(Context* context,
                                                     std::shared_ptr<Window> window) {
  if (window == nullptr) {
    return nullptr;
  }
  auto renderTarget = window->onCreateRenderTarget(context);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto colorSpace = window->colorSpace();
  return std::shared_ptr<WindowDrawable>(
      new WindowDrawable(context, std::move(renderTarget), std::move(colorSpace)));
}

WindowDrawable::WindowDrawable(Context* context, std::shared_ptr<RenderTargetProxy> renderTarget,
                               std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(context, std::move(renderTarget), std::move(colorSpace)) {
}

WindowDrawable::~WindowDrawable() {
  present();
}

void WindowDrawable::onPresent() {
  getWindow()->onPresent(getContext(), getRenderTarget());
}

}  // namespace tgfx
