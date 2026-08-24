/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "WGLDevice.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {
class WGLWindow : public Window {
 public:
  /**
   * Creates a new window from a HWND with specialed shared context.
   * @param nativeWindow The native window handle to render into.
   * @param sharedContext An optional shared WGL context.
   * @param colorSpace An optional color space for rendering. If nullptr, the default sRGB is used.
   * @param vsyncEnabled Whether presentation is synchronized to the display's refresh rate. Fixed
   * for the lifetime of the window. Defaults to true.
   */
  static std::shared_ptr<WGLWindow> MakeFrom(HWND nativeWindow, HGLRC sharedContext = nullptr,
                                             std::shared_ptr<ColorSpace> colorSpace = nullptr,
                                             bool vsyncEnabled = true);

 protected:
  std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) override;
  void onPresent(Context* context) override;

 private:
  HWND nativeWindow = nullptr;
  // wglSwapInterval requires the context to be current, which is only guaranteed at present time,
  // so the fixed vsync setting is applied once on the first present.
  bool swapIntervalApplied = false;

  explicit WGLWindow(std::shared_ptr<Device> device,
                     std::shared_ptr<ColorSpace> colorSpace = nullptr, bool vsyncEnabled = true);
};
}  // namespace tgfx
