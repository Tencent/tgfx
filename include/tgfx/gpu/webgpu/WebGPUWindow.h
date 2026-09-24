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

  /**
   * Creates a new window from an existing canvas, which may be an OffscreenCanvas.
   *
   * Can be created from any thread that holds the canvas, including a worker thread; the window
   * then belongs to that thread and the canvas has to stay alive for as long as it does. Nothing
   * has to be done to present the result: if the canvas is shown on the page, the browser displays
   * what is rendered into it.
   *
   * On Web, the final executable must export the WebGPU runtime method (see the Web build section
   * in README.md). Both overloads need it: without it the selector overload above renders without
   * color space configuration and without video texture uploads, and this overload cannot create the
   * surface at all and returns nullptr instead.
   *
   * @param canvas An HTMLCanvasElement or an OffscreenCanvas. Returns nullptr if it is null or the
   *     window cannot be created.
   * @param device An optional WebGPUDevice. Required on a thread that cannot obtain the default
   *     device, such as a worker thread. If nullptr, a default device is created automatically.
   * @param colorSpace An optional target color space for the drawing buffer. If nullptr, the
   *     default sRGB color space is used. When a non-null color space is provided, the canvas's
   *     WebGPU context is reconfigured with the color space so that the rendered content is
   *     displayed correctly.
   */
  static std::shared_ptr<WebGPUWindow> MakeFrom(emscripten::val canvas,
                                                std::shared_ptr<WebGPUDevice> device = nullptr,
                                                std::shared_ptr<ColorSpace> colorSpace = nullptr);

  ~WebGPUWindow() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) override;
  void onPresent(Context* context) override;

 private:
  WebGPUWindow(std::shared_ptr<Device> device, void* surface, int width, int height,
               const std::string& canvasSelector, std::shared_ptr<ColorSpace> colorSpace);

  // Shared tail of both MakeFrom() overloads. The canvas is null on the selector path, in which
  // case the surface and the drawing buffer size come from the page instead.
  static std::shared_ptr<WebGPUWindow> MakeFromCanvas(emscripten::val canvas,
                                                      const std::string& canvasSelector,
                                                      std::shared_ptr<WebGPUDevice> device,
                                                      std::shared_ptr<ColorSpace> colorSpace);

  // Configures the canvas's WebGPU context to use the target color space. Must be called after
  // each wgpuSurfaceConfigure() call, since the emscripten surface configuration does not carry
  // the color space information.
  void configureColorSpace(WGPUTextureFormat format, WGPUTextureUsageFlags usage,
                           WGPUCompositeAlphaMode alphaMode);

  std::string _canvasSelector;
  // Set only by the canvas object overload, in which case the drawing buffer size and the WebGPU
  // context both come from this canvas rather than from the page.
  emscripten::val _canvas;
  void* _surface = nullptr;
  int _width = 0;
  int _height = 0;
  int _configuredWidth = 0;
  int _configuredHeight = 0;
  std::shared_ptr<RenderTargetProxy> drawableProxy = nullptr;
};

}  // namespace tgfx
