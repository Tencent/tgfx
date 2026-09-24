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

#include <emscripten/html5_webgl.h>
#include <emscripten/val.h>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/opengl/GLDevice.h"

namespace tgfx {
class WebGLDevice : public GLDevice {
 public:
  /**
   * Creates a WebGLDevice from the id of a canvas element in the document.
   *
   * Can only be called from the main thread. Use the canvas overload below to create a device on a
   * worker thread.
   */
  static std::shared_ptr<WebGLDevice> MakeFrom(const std::string& canvasID,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a WebGLDevice from an existing canvas, which may be an OffscreenCanvas.
   *
   * Can be called from any thread that holds the canvas, including a worker thread. The device
   * belongs to the calling thread: it has to be rendered with and destroyed on that thread, and the
   * canvas has to stay alive for as long as the device does.
   *
   * On Web, the final executable must export the GL runtime method (see the Web build section in
   * README.md); without it no context can be created.
   *
   * @param canvas An HTMLCanvasElement or an OffscreenCanvas. Returns nullptr if it is null or no
   *     context can be created from it.
   * @param colorSpace An optional color space for rendering. If nullptr, the default sRGB is used.
   */
  static std::shared_ptr<WebGLDevice> MakeFrom(emscripten::val canvas,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  ~WebGLDevice() override;

  bool sharableWith(void* nativeHandle) const override;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE oldContext = 0;

  // Shared tail of both MakeFrom() overloads: makes the context current, applies the color space and
  // wraps the handle into a device that owns it.
  static std::shared_ptr<WebGLDevice> MakeFromContext(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context,
                                                      std::shared_ptr<ColorSpace> colorSpace);

  static std::shared_ptr<WebGLDevice> Wrap(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context,
                                           bool externallyOwned);

  WebGLDevice(std::unique_ptr<GPU> gpu, EMSCRIPTEN_WEBGL_CONTEXT_HANDLE nativeHandle);

  friend class GLDevice;
  friend class WebGLWindow;
};
}  // namespace tgfx
