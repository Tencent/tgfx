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

#include "tgfx/gpu/opengl/glfw/GLFWGLDevice.h"
#include "gpu/opengl/glfw/GLFWGLGPU.h"

namespace tgfx {

void* GLDevice::CurrentNativeHandle() {
  return static_cast<void*>(glfwGetCurrentContext());
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  auto glContext = glfwGetCurrentContext();
  auto glDevice = GLDevice::Get(glContext);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<GLFWGLDevice>(glDevice);
  }
  return GLFWGLDevice::Wrap(glContext, true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  // TODO: GLDevice::Make is not implemented
  return nullptr;
}

std::shared_ptr<GLFWGLDevice> GLFWGLDevice::MakeFrom(GLFWwindow* glContext, bool adopted) {
  return GLFWGLDevice::Wrap(glContext, !adopted);
}

std::shared_ptr<GLFWGLDevice> GLFWGLDevice::Wrap(GLFWwindow* glContext, bool externallyOwned) {
  auto glDevice = GLDevice::Get(glContext);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<GLFWGLDevice>(glDevice);
  }
  if (glContext == nullptr) {
    return nullptr;
  }

  auto oldGLContext = glfwGetCurrentContext();
  if (oldGLContext != glContext) {
    glfwMakeContextCurrent(glContext);
  }
  std::shared_ptr<GLFWGLDevice> device = nullptr;
  auto interface = GLInterface::GetNative();
  if (interface != nullptr) {
    auto gpu = std::make_unique<GLFWGLGPU>(std::move(interface));
    device = std::shared_ptr<GLFWGLDevice>(new GLFWGLDevice(std::move(gpu), glContext));
    device->externallyOwned = externallyOwned;
    device->glContext = glContext;
  }
  if (oldGLContext != glContext) {
    glfwMakeContextCurrent(oldGLContext);
  }
  return device;
}

GLFWGLDevice::GLFWGLDevice(std::unique_ptr<GPU> gpu, GLFWwindow* glContext)
    : GLDevice(std::move(gpu), glContext), glContext(glContext) {
}

GLFWGLDevice::~GLFWGLDevice() {
  releaseAll();
  if (externallyOwned) {
    return;
  }
  if (glContext != nullptr) {
    glfwDestroyWindow(glContext);
    glContext = nullptr;
  }
}

bool GLFWGLDevice::sharableWith(void* glContext) const {
  // TODO: GLFWGLDevice::sharableWith is not implemented
  return false;
}

bool GLFWGLDevice::onLockContext() {
  oldGLContext = glfwGetCurrentContext();
  if (oldGLContext == glContext) {
    return true;
  }
  glfwMakeContextCurrent(glContext);
  return true;
}

void GLFWGLDevice::onUnlockContext() {
  if (oldGLContext == glContext) {
    return;
  }
  glfwMakeContextCurrent(oldGLContext);
}

}  // namespace tgfx