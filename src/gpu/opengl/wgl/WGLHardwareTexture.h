/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "gpu/opengl/GLTexture.h"
#include "gpu/opengl/wgl/WGLGPU.h"

namespace tgfx {
/**
 * A GLTexture subclass that wraps a D3D11 texture imported into OpenGL via
 * GL_EXT_memory_object or WGL_NV_DX_interop. Manages interop resource cleanup on release.
 */
class WGLHardwareTexture : public GLTexture {
 public:
  static std::shared_ptr<WGLHardwareTexture> MakeFrom(WGLGPU* gpu, HardwareBufferRef hardwareBuffer,
                                                      uint32_t usage, int planeIndex = 0);
  ~WGLHardwareTexture() override;

  HardwareBufferRef getHardwareBuffer() const override {
    return hardwareBuffer;
  }

 protected:
  void onReleaseTexture(GLGPU* gpu) override;

 private:
  HardwareBufferRef hardwareBuffer = nullptr;
  unsigned memoryObject = 0;
  void* interopDevice = nullptr;
  void* d3d11Device = nullptr;
  void* interopTexture = nullptr;

  WGLHardwareTexture(const TextureDescriptor& descriptor, HardwareBufferRef hardwareBuffer,
                     unsigned target, unsigned textureID);
  friend class GLGPU;
};
}  // namespace tgfx
