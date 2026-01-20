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

#include "WebGPUDevice.h"
#include "tgfx/gpu/Window.h"
namespace tgfx {
class WebGPUWindow : public Window {
 public:
  /**
   * Creates a new window from a canvas.
   */
  static std::shared_ptr<Window> MakeFrom(const std::string& canvasID,
                                          std::shared_ptr<ColorSpace> colorSpace = nullptr);

 protected:
  std::shared_ptr<Surface> onCreateSurface(Context* context) override;

  void onPresent(Context* context) override;

 private:
  std::string canvasID;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  explicit WebGPUWindow(std::shared_ptr<Device> device,
                        std::shared_ptr<ColorSpace> colorSpace = nullptr);
};
}  // namespace tgfx
