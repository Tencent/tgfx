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

#pragma once

#include <memory>
#include "tgfx/gpu/Drawable.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {

/**
 * The generic Drawable implementation returned by Window::onNextDrawable(): it adapts the frame
 * target and the presentation through a RenderTargetProxy created by the window. Backends with
 * native frame objects (MetalDrawable, VulkanDrawable) subclass Drawable directly instead; they
 * do not inherit from this class, which depends on the window's default target creation path.
 * The frame is resolved through the window's onCreateRenderTarget() when it is imported into a
 * Context, which keeps GL and EAGL target creation on a current context.
 */
class ProxyDrawable : public Drawable {
 public:
  static std::shared_ptr<ProxyDrawable> Make(std::shared_ptr<Window> window);

  ~ProxyDrawable() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onImport(Context* context) override;

 private:
  ProxyDrawable(std::shared_ptr<ColorSpace> colorSpace);
};

}  // namespace tgfx
