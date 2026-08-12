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
 * A D3D12Texture that wraps an ID3D11Texture2D imported via a shared NT handle. The source
 * texture must have been created with D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
 * D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX. Only TextureUsage::TEXTURE_BINDING is supported.
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
