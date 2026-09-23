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
   * Creates a new window from the id of a canvas element in the document.
   * On Web, the final executable must export the GL runtime method (see the Web build section in
   * README.md); otherwise color space configuration and image/video texture uploads will not work.
   *
   * Can only be created from the main thread. Use the canvas overload below to create a window on a
   * worker thread.
   */
  static std::shared_ptr<WebGLWindow> MakeFrom(const std::string& canvasID,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a new window from an existing canvas, which may be an OffscreenCanvas.
   *
   * Can be created from any thread that holds the canvas, including a worker thread; the window then
   * belongs to that thread and the canvas has to stay alive for as long as it does. Nothing has to be
   * done to present the result: if the canvas is shown on the page, the browser displays what is
   * rendered into it.
   *
   * @param canvas An HTMLCanvasElement or an OffscreenCanvas. Returns nullptr if it is null or the
   *     window cannot be created.
   * @param colorSpace An optional color space for rendering. If nullptr, the default sRGB is used.
   */
  static std::shared_ptr<WebGLWindow> MakeFrom(emscripten::val canvas,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

 protected:
  std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) override;

 private:
  std::string canvasID;
  // Set only by the canvas object overload, in which case the render target size comes from this
  // canvas rather than from the page.
  emscripten::val canvas;

  explicit WebGLWindow(std::shared_ptr<Device> device,
                       std::shared_ptr<ColorSpace> colorSpace = nullptr);
};
}  // namespace tgfx
