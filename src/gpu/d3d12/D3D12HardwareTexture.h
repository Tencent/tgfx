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

#include "D3D12Texture.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {

/**
 * A D3D12Texture subclass that wraps an ID3D11Texture2D imported into D3D12 via a shared NT
 * handle. The source ID3D11Texture2D must have been created with
 * D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX. NTHANDLE alone is
 * rejected by the D3D11 runtime (E_INVALIDARG), and the keyed mutex is what serialises access
 * between the D3D11 producer and the D3D12 consumer.
 *
 * Only TextureUsage::TEXTURE_BINDING is supported: the resulting D3D12 resource is intended for
 * sampling, not for use as a render target.
 *
 * Keyed-mutex contract (key is hard-coded to 0, matching WGL_NV_DX_interop / D3D11On12 / Skia):
 *   - The application produces content on the D3D11 side, then calls
 *     IDXGIKeyedMutex::ReleaseSync(0) to hand ownership to D3D12.
 *   - tgfx internally calls AcquireSync(0, 5000ms) before each ExecuteCommandLists that samples
 *     this texture and ReleaseSync(0) once the GPU has finished (after the fence signals).
 *   - The application must call AcquireSync(0, timeout) again before it modifies the texture on
 *     the D3D11 side.
 * If AcquireSync times out on the tgfx side (typically due to a missing ReleaseSync in the
 * application), the D3D12 context is marked lost.
 */
class D3D12HardwareTexture : public D3D12Texture {
 public:
  /**
   * Imports the given HardwareBufferRef (ID3D11Texture2D*) as a D3D12 texture. Returns nullptr if
   * the buffer is invalid, its format is unsupported, the caller requested a usage other than
   * TEXTURE_BINDING, or the underlying D3D11 texture is not shareable via NT handle.
   */
  static std::shared_ptr<D3D12HardwareTexture> MakeFrom(D3D12GPU* gpu,
                                                        HardwareBufferRef hardwareBuffer,
                                                        uint32_t usage);

  ~D3D12HardwareTexture() override;

  HardwareBufferRef getHardwareBuffer() const override {
    return hardwareBuffer;
  }

  IDXGIKeyedMutex* keyedMutex() const override {
    return _keyedMutex.Get();
  }

 protected:
  void onReleaseTexture() override;

 private:
  D3D12HardwareTexture(const TextureDescriptor& descriptor, ComPtr<ID3D12Resource> resource,
                       unsigned dxgiFormat, ComPtr<IDXGIKeyedMutex> keyedMutex,
                       HardwareBufferRef hardwareBuffer);

  HardwareBufferRef hardwareBuffer = nullptr;
  ComPtr<IDXGIKeyedMutex> _keyedMutex = nullptr;

  friend class D3D12GPU;
};

}  // namespace tgfx
