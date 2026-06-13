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

#include "D3D12Resource.h"
#include "D3D12Util.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {

class D3D12GPU;

/**
 * D3D12 texture implementation.
 */
class D3D12Texture : public Texture, public D3D12Resource {
 public:
  static std::shared_ptr<D3D12Texture> Make(D3D12GPU* gpu, const TextureDescriptor& descriptor);

  /**
   * Creates a D3D12Texture wrapper from an external D3D12 resource.
   */
  static std::shared_ptr<D3D12Texture> MakeFrom(D3D12GPU* gpu, ComPtr<ID3D12Resource> resource,
                                                unsigned dxgiFormat, uint32_t usage, bool adopted);

  /**
   * Returns the underlying D3D12 resource.
   */
  ID3D12Resource* d3d12Resource() const {
    return resource.Get();
  }

  /**
   * Returns the DXGI format of this texture.
   */
  unsigned dxgiFormat() const {
    return _dxgiFormat;
  }

  /**
   * Returns the resource state currently tracked on the CPU. D3D12, unlike Vulkan, requires the
   * application to issue explicit ResourceBarrier calls to transition between read and write
   * states. We track the most recently announced state per texture so that subsequent bindings
   * can construct the correct transition barrier.
   *
   * Note: on textures imported from external D3D12 resources we initialise the state to COMMON
   * (the value the application is required to leave the resource in when handing it off — see
   * D3D12 SDK common-state promotion rules). This is conservative but correct.
   */
  D3D12_RESOURCE_STATES currentState() const {
    return _currentState;
  }

  void setCurrentState(D3D12_RESOURCE_STATES state) {
    _currentState = state;
  }

  BackendTexture getBackendTexture() const override;
  BackendRenderTarget getBackendRenderTarget() const override;

 protected:
  D3D12Texture(const TextureDescriptor& descriptor, ComPtr<ID3D12Resource> resource,
               unsigned dxgiFormat);
  ~D3D12Texture() override = default;

  void onRelease(D3D12GPU* gpu) override;

  virtual void onReleaseTexture();

  ComPtr<ID3D12Resource> resource = nullptr;
  unsigned _dxgiFormat = 0;
  D3D12_RESOURCE_STATES _currentState = D3D12_RESOURCE_STATE_COMMON;

  friend class D3D12GPU;
};

}  // namespace tgfx
