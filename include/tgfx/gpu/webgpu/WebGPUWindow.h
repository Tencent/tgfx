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

#include <webgpu/webgpu.h>
#include "tgfx/gpu/Window.h"
#include "tgfx/gpu/webgpu/WebGPUDevice.h"

namespace tgfx {

class WebGPUWindow : public Window {
 public:
  /**
   * Creates a new window from an HTML canvas element selector with the specified device.
   * @param canvasSelector The CSS selector for the HTML canvas element (e.g., "#myCanvas").
   * @param device An optional WebGPUDevice. If nullptr, a default device is created automatically.
   * @param colorSpace An optional target color space for the drawing buffer. If nullptr, the
   * default sRGB color space is used. When a non-null color space is provided, the canvas's WebGPU
   * context is reconfigured with the color space so that the rendered content is displayed
   * correctly. Both the default device and devices passed in via WebGPUDevice::MakeFrom() are
   * supported.
   * On Web, the final executable must export the WebGPU runtime method (see the Web build section
   * in README.md); otherwise color space configuration and video texture uploads will not work.
   *
   * Note: this backend does not expose a vsync option. Browser canvas presentation only supports
   * Fifo, so presentation is always synchronized to the display refresh rate and vsyncEnabled()
   * always returns true.
   */
  static std::shared_ptr<WebGPUWindow> MakeFrom(const std::string& canvasSelector,
                                                std::shared_ptr<WebGPUDevice> device = nullptr,
                                                std::shared_ptr<ColorSpace> colorSpace = nullptr);

  ~WebGPUWindow() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) override;
  void onPresent(Context* context) override;

 private:
  WebGPUWindow(std::shared_ptr<Device> device, void* surface, int width, int height,
               const std::string& canvasSelector, std::shared_ptr<ColorSpace> colorSpace);

  // Configures the canvas's WebGPU context to use the target color space. Must be called after
  // each wgpuSurfaceConfigure() call, since the emscripten surface configuration does not carry
  // the color space information.
  void configureColorSpace(WGPUTextureFormat format, WGPUTextureUsageFlags usage,
                           WGPUCompositeAlphaMode alphaMode);

  std::string _canvasSelector;
  void* _surface = nullptr;
  int _width = 0;
  int _height = 0;
  int _configuredWidth = 0;
  int _configuredHeight = 0;
  std::shared_ptr<RenderTargetProxy> drawableProxy = nullptr;
};

}  // namespace tgfx
