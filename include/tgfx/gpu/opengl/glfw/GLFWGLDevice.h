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

#include <GLFW/glfw3.h>
#include "tgfx/gpu/opengl/GLDevice.h"

namespace tgfx {
class GLFWGLDevice : public GLDevice {
 public:
  /**
   * Creates a GLFWGLDevice with the existing GLFWwindow. If adopted is true, the GLFWGLDevice 
   * will take ownership of the GLFWwindow and destroy them when the GLFWGLDevice is destroyed.
   * The caller should not destroy the GLFWwindow in this case.
   */
  static std::shared_ptr<GLFWGLDevice> MakeFrom(GLFWwindow* glContext, bool adopted = false);

  ~GLFWGLDevice() override;

  bool sharableWith(void* glContext) const override;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  GLFWwindow* glContext = nullptr;
  GLFWwindow* oldGLContext = nullptr;

  GLFWGLDevice(std::unique_ptr<GPU> gpu, GLFWwindow* glContext);

  static std::shared_ptr<GLFWGLDevice> Wrap(GLFWwindow* glContext, bool externallyOwned);

  friend class GLDevice;
  friend class GLFWGLWindow;
};
}  // namespace tgfx
