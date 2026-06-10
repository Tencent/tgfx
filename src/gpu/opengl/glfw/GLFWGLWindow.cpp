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

#include "tgfx/gpu/opengl/glfw/GLFWGLWindow.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<GLFWGLWindow> GLFWGLWindow::MakeFrom(GLFWwindow* glContext) {
  if (glContext == nullptr) {
    return nullptr;
  }

  auto device = GLFWGLDevice::MakeFrom(glContext);
  if (device == nullptr) {
    return nullptr;
  }

  auto glWindow = std::shared_ptr<GLFWGLWindow>(new GLFWGLWindow(device));
  glWindow->glContext = glContext;
  return glWindow;
}

GLFWGLWindow::GLFWGLWindow(std::shared_ptr<Device> device) : Window(std::move(device)) {
}

std::shared_ptr<RenderTargetProxy> GLFWGLWindow::onCreateRenderTarget(Context* context) {
  ISize size = {0, 0};
  if (glContext) {
    glfwGetFramebufferSize(glContext, &size.width, &size.height);
  }
  if (size.width <= 0 || size.height <= 0) {
    return nullptr;
  }

  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  BackendRenderTarget renderTarget(frameBuffer, size.width, size.height);
  return RenderTargetProxy::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft);
}

void GLFWGLWindow::onPresent(Context*) {
  const auto glDevice = std::static_pointer_cast<GLFWGLDevice>(this->device);
  glfwSwapBuffers(glDevice->glContext);
}

}  // namespace tgfx
