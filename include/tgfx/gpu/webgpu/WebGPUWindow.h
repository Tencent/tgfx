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
   * Creates a window for a canvas selected from the document.
   * @param canvasSelector CSS selector for the canvas.
   * @param device Device to render with; defaults to the WebGPU default device.
   * @param colorSpace Optional drawing-buffer color space; defaults to sRGB.
   * Web builds require the WebGPU runtime method for color-space configuration and video uploads.
   * Presentation is synchronized to the display refresh rate.
   */
  static std::shared_ptr<WebGPUWindow> MakeFrom(const std::string& canvasSelector,
                                                std::shared_ptr<WebGPUDevice> device = nullptr,
                                                std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a window for a canvas held by the calling thread. The window must be used on that
   * thread, and the canvas must outlive it. The browser presents directly from the canvas. Returns
   * nullptr if creation fails. Web builds require the WebGPU runtime method; see README.md.
   * @param canvas HTMLCanvasElement or OffscreenCanvas.
   * @param device Device to render with; required on threads without a default device.
   * @param colorSpace Optional drawing-buffer color space; defaults to sRGB.
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
