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
   */
  static std::shared_ptr<CGLWindow> MakeFrom(NSView* view, CGLContextObj sharedContext = nullptr,
                                             std::shared_ptr<ColorSpace> colorSpace = nullptr);

  ~CGLWindow() override;

 protected:
  std::shared_ptr<Surface> onCreateSurface(Context* context) override;
  void onPresent(Context* context) override;

 private:
  NSView* view = nil;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  CGLWindow(std::shared_ptr<Device> device, NSView* view,
            std::shared_ptr<ColorSpace> colorSpace = nullptr);
};
}  // namespace tgfx
