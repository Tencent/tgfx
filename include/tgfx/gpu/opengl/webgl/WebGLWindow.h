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
   * Creates a window for a canvas in the document. Main-thread only. Web builds require the GL
   * runtime method for color-space configuration and image/video uploads; see README.md.
   */
  static std::shared_ptr<WebGLWindow> MakeFrom(const std::string& canvasID,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a window for a canvas held by the calling thread. The canvas must outlive the window,
   * which must be used on that thread. The browser presents directly from the canvas. Returns
   * nullptr if creation fails. Web builds require the GL runtime method; see README.md.
   *
   * @param canvas An HTMLCanvasElement or OffscreenCanvas.
   * @param colorSpace Optional rendering color space; defaults to sRGB.
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
