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

#pragma once

#include "CGLDevice.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {
class CGLWindow : public Window {
 public:
  /**
   * Creates a new window from an NSView with specified shared context.
   * @param view The NSView to render into. Must not be nil.
   * @param sharedContext An optional shared CGL context. If nullptr, a new context is created.
   * @param colorSpace An optional color space for rendering. If nullptr, the default sRGB is used.
   * @param sampleCount The number of samples for MSAA rendering. Defaults to 1 (no MSAA).
   */
  static std::shared_ptr<CGLWindow> MakeFrom(NSView* view, CGLContextObj sharedContext = nullptr,
                                             std::shared_ptr<ColorSpace> colorSpace = nullptr,
                                             int sampleCount = 1);

  ~CGLWindow() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) override;
  void onPresent(Context* context) override;

 private:
  NSView* view = nil;

  explicit CGLWindow(std::shared_ptr<Device> device, NSView* view,
                     std::shared_ptr<ColorSpace> colorSpace, int sampleCount);
};
}  // namespace tgfx
