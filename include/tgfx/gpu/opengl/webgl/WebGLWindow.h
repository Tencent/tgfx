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

#include "WebGLDevice.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {
class WebGLWindow : public Window {
 public:
  /**
   * Creates a new window from a canvas.
   * On Web, the final executable must export the GL runtime method (see the Web build section in
   * README.md); otherwise color space configuration and image/video texture uploads will not work.
   *
   * The id is resolved through the Emscripten canvas lookup, which needs a DOM. Use the canvas
   * overload below on a thread that has none.
   */
  static std::shared_ptr<WebGLWindow> MakeFrom(const std::string& canvasID,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a new window from an existing canvas object.
   *
   * The device is created straight from the canvas and the drawing buffer size is read from it too,
   * so nothing here needs a DOM. This is the entry point for rendering into an OffscreenCanvas from
   * a worker; pair it with WebGLDevice::MakeFrom(emscripten::val) and run both on the thread that
   * owns the canvas.
   *
   * @param canvas An HTMLCanvasElement or an OffscreenCanvas. Returns nullptr if it is null or no
   *     device can be created from it.
   * @param colorSpace An optional color space for rendering. If nullptr, the default sRGB is used.
   */
  static std::shared_ptr<WebGLWindow> MakeFrom(emscripten::val canvas,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

 protected:
  std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) override;

 private:
  std::string canvasID;
  // Set only by the canvas object overload; when it is set the render target size comes from here
  // instead of from the Emscripten canvas lookup.
  emscripten::val canvas;

  explicit WebGLWindow(std::shared_ptr<Device> device,
                       std::shared_ptr<ColorSpace> colorSpace = nullptr);
};
}  // namespace tgfx
