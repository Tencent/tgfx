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

#pragma once

#include <memory>
#include "tgfx/gpu/Drawable.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {

/**
 * The default Drawable implementation returned by Window::onNextDrawable(). The frame is resolved
 * through the window's onCreateRenderTarget() when it is imported into a Context, which keeps GL
 * and EAGL target creation on a current context, and presented through the window's
 * onPresent() with the frame's own render target.
 */
class WindowDrawable : public Drawable {
 public:
  static std::shared_ptr<WindowDrawable> Make(std::shared_ptr<Window> window);

  ~WindowDrawable() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onImport(Context* context) override;
  void onPresent(Context* context) override;

 private:
  WindowDrawable(std::shared_ptr<ColorSpace> colorSpace);
};

}  // namespace tgfx
