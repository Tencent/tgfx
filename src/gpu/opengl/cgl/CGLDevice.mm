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

#include "tgfx/gpu/opengl/cgl/CGLDevice.h"
#include "gpu/opengl/cgl/CGLGPU.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace tgfx {
void* GLDevice::CurrentNativeHandle() {
  return CGLGetCurrentContext();
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  return CGLDevice::Wrap(CGLGetCurrentContext(), true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  CGLPixelFormatObj format = nullptr;
  CGLContextObj cglShareContext = reinterpret_cast<CGLContextObj>(sharedContext);
  if (cglShareContext == nullptr) {
    const CGLPixelFormatAttribute attributes[] = {
        kCGLPFAStencilSize,        (CGLPixelFormatAttribute)8,
        kCGLPFAAccelerated,        kCGLPFADoubleBuffer,
        kCGLPFAOpenGLProfile,      (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
        (CGLPixelFormatAttribute)0};
    GLint npix = 0;
    CGLChoosePixelFormat(attributes, &format, &npix);
  } else {
    format = CGLGetPixelFormat(cglShareContext);
  }
  CGLContextObj cglContext = nullptr;
  CGLCreateContext(format, cglShareContext, &cglContext);
  if (cglShareContext == nullptr) {
    CGLDestroyPixelFormat(format);
  }
  if (cglContext == nullptr) {
    return nullptr;
  }
  GLint opacity = 0;
  CGLSetParameter(cglContext, kCGLCPSurfaceOpacity, &opacity);
  auto device = CGLDevice::Wrap(cglContext, false);
  CGLReleaseContext(cglContext);
  return device;
}

std::shared_ptr<CGLDevice> CGLDevice::MakeFrom(CGLContextObj cglContext) {
  return CGLDevice::Wrap(cglContext, true);
}

std::shared_ptr<CGLDevice> CGLDevice::Wrap(CGLContextObj cglContext, bool externallyOwned) {
  if (cglContext == nil) {
    return nullptr;
  }
  auto glDevice = GLDevice::Get(cglContext);
  if (glDevice) {
    return std::static_pointer_cast<CGLDevice>(glDevice);
  }
  @autoreleasepool {
    auto oldCGLContext = CGLGetCurrentContext();
    if (oldCGLContext != cglContext) {
      CGLSetCurrentContext(cglContext);
      if (CGLGetCurrentContext() != cglContext) {
        return nullptr;
      }
    }
    std::shared_ptr<CGLDevice> device = nullptr;
    auto interface = GLInterface::GetNative();
    if (interface != nullptr) {
      auto gpu = std::make_unique<CGLGPU>(std::move(interface), cglContext);
      device = std::shared_ptr<CGLDevice>(new CGLDevice(std::move(gpu), cglContext));
      device->externallyOwned = externallyOwned;
      device->weakThis = device;
    }
    if (oldCGLContext != cglContext) {
      CGLSetCurrentContext(oldCGLContext);
    }
    return device;
  }
}

CGLDevice::CGLDevice(std::unique_ptr<GPU> gpu, CGLContextObj cglContext)
    : GLDevice(std::move(gpu), cglContext) {
  glContext = [[NSOpenGLContext alloc] initWithCGLContextObj:cglContext];
}

CGLDevice::~CGLDevice() {
  releaseAll();
  [glContext release];
}

bool CGLDevice::sharableWith(void* nativeContext) const {
  if (nativeContext == nullptr) {
    return false;
  }
  auto shareContext = static_cast<CGLContextObj>(nativeContext);
  return CGLGetShareGroup(shareContext) == CGLGetShareGroup(glContext.CGLContextObj);
}

CGLContextObj CGLDevice::cglContext() const {
  return glContext.CGLContextObj;
}

bool CGLDevice::onLockContext() {
  @autoreleasepool {
    oldContext = CGLGetCurrentContext();
    CGLRetainContext(oldContext);
    [glContext makeCurrentContext];
    return [NSOpenGLContext currentContext] == glContext;
  }
}

void CGLDevice::onUnlockContext() {
  [NSOpenGLContext clearCurrentContext];
  CGLSetCurrentContext(oldContext);
  CGLReleaseContext(oldContext);
}
}  // namespace tgfx

#pragma clang diagnostic pop
