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
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/opengl/GLDevice.h"

namespace tgfx {
class WebGLDevice : public GLDevice {
 public:
  /**
   * Creates a WebGLDevice from the id of an existing HTMLCanvasElement.
   */
  static std::shared_ptr<WebGLDevice> MakeFrom(const std::string& canvasID,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  ~WebGLDevice() override;

  bool sharableWith(void* nativeHandle) const override;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE oldContext = 0;

  static std::shared_ptr<WebGLDevice> Wrap(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context,
                                           bool externallyOwned);

  WebGLDevice(std::unique_ptr<GPU> gpu, EMSCRIPTEN_WEBGL_CONTEXT_HANDLE nativeHandle);

  friend class GLDevice;
  friend class WebGLWindow;
};
}  // namespace tgfx
