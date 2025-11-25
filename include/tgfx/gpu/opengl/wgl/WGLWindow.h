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
   */
  static std::shared_ptr<WGLWindow> MakeFrom(HWND nativeWindow, HGLRC sharedContext = nullptr);

 protected:
  std::shared_ptr<Surface> onCreateSurface(Context* context,
                                           std::shared_ptr<ColorSpace> colorSpace) override;
  void onPresent(Context*) override;

 private:
  HWND nativeWindow = nullptr;

  explicit WGLWindow(std::shared_ptr<Device> device);
};
}  // namespace tgfx
