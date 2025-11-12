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

#if defined(__ANDROID__) || defined(ANDROID) || defined(__OHOS__)

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "gpu/opengl/GLTexture.h"
#include "gpu/opengl/egl/EGLGPU.h"

namespace tgfx {
class EGLHardwareTexture : public GLTexture {
 public:
  static std::shared_ptr<EGLHardwareTexture> MakeFrom(EGLGPU* gpu, HardwareBufferRef hardwareBuffer,
                                                      uint32_t usage);
  ~EGLHardwareTexture() override;

  HardwareBufferRef getHardwareBuffer() const override {
    return hardwareBuffer;
  }

 protected:
  void onReleaseTexture(GLGPU* gpu) override;

 private:
  HardwareBufferRef hardwareBuffer = nullptr;
  EGLImageKHR eglImage = EGL_NO_IMAGE_KHR;

  EGLHardwareTexture(const TextureDescriptor& descriptor, HardwareBufferRef hardwareBuffer,
                     EGLImageKHR eglImage, unsigned target, unsigned textureID);
  friend class GLGPU;
};
}  // namespace tgfx

#endif