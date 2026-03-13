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

#include <memory>
#include "gpu/opengl/GLGPU.h"

struct ID3D11Device;

// Forward-declare HANDLE without pulling in windows.h.
// HANDLE is defined as void* in the Windows SDK (winnt.h).
#ifndef DECLARE_HANDLE
typedef void* HANDLE;
#endif

namespace tgfx {

// Forward declaration to keep WGL/Windows types out of this header.
struct WGLInteropState;

class WGLGPU : public GLGPU {
 public:
  explicit WGLGPU(std::shared_ptr<GLInterface> glInterface);
  ~WGLGPU() override;

  std::vector<std::shared_ptr<Texture>> importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                               uint32_t usage) override;

  /**
   * Returns true if the WGL_NV_DX_interop extension is available in the current GL context.
   * Performs lazy detection and caches the result in interopState.
   */
  bool isNVDXInteropAvailable();

  /**
   * Returns true if the GL_EXT_memory_object and GL_EXT_memory_object_win32 extensions are
   * available in the current GL context. Performs lazy detection and caches the result.
   */
  bool isMemoryObjectInteropAvailable();

  /**
   * Returns a shared WGL interop device handle for the given D3D11 device, opening a new one via
   * wglDXOpenDeviceNV if needed. Each call increments an internal ref-count; the caller must
   * eventually call releaseSharedInteropDevice with the same pointers.
   */
  HANDLE acquireSharedInteropDevice(ID3D11Device* d3d11Device);

  /**
   * Decrements the ref-count for the given interop device. When the count reaches zero the device
   * is closed via wglDXCloseDeviceNV.
   */
  void releaseSharedInteropDevice(HANDLE interopDevice, ID3D11Device* d3d11Device);

  WGLInteropState* getInteropState() const {
    return interopState.get();
  }

 private:
  std::unique_ptr<WGLInteropState> interopState;
};

}  // namespace tgfx
