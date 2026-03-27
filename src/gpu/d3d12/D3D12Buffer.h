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
#include "tgfx/gpu/GPUBuffer.h"

namespace tgfx {

class D3D12GPU;

/**
 * D3D12 buffer implementation.
 */
class D3D12Buffer : public GPUBuffer, public D3D12Resource {
 public:
  static std::shared_ptr<D3D12Buffer> Make(D3D12GPU* gpu, size_t size, uint32_t usage);

  /**
   * Returns the underlying D3D12 resource.
   */
  ID3D12Resource* d3d12Resource() const {
    return resource.Get();
  }

  void* map(size_t offset, size_t size) override;
  void unmap() override;
  bool isReady() const override;

 protected:
  void onRelease(D3D12GPU* gpu) override;

 private:
  D3D12Buffer(size_t size, uint32_t usage, ComPtr<ID3D12Resource> resource);
  ~D3D12Buffer() override = default;

  ComPtr<ID3D12Resource> resource = nullptr;
  void* mappedPointer = nullptr;

  friend class D3D12GPU;
};

}  // namespace tgfx
