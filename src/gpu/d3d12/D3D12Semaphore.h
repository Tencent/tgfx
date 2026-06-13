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
#include "tgfx/gpu/Semaphore.h"

namespace tgfx {

class D3D12GPU;

/**
 * D3D12 semaphore implementation backed by an ID3D12Fence and a target signal value. Once the
 * fence reaches the target value, all GPU work submitted before the signal is guaranteed to have
 * completed, providing the same semantics as a Vulkan timeline semaphore.
 */
class D3D12Semaphore : public Semaphore, public D3D12Resource {
 public:
  static std::shared_ptr<D3D12Semaphore> Make(D3D12GPU* gpu);

  static std::shared_ptr<D3D12Semaphore> MakeFrom(D3D12GPU* gpu, ComPtr<ID3D12Fence> fence,
                                                  uint64_t value);

  D3D12Semaphore(ComPtr<ID3D12Fence> fence, uint64_t value, bool adopted);
  ~D3D12Semaphore() override = default;

  ID3D12Fence* d3d12Fence() const {
    return _fence.Get();
  }

  uint64_t signalValue() const {
    return _value;
  }

  uint64_t nextSignalValue() const {
    return _value + 1;
  }

  void commitSignalValue() {
    ++_value;
  }

  BackendSemaphore getBackendSemaphore() const override;

 protected:
  void onRelease(D3D12GPU* gpu) override;

 private:
  ComPtr<ID3D12Fence> _fence = nullptr;
  uint64_t _value = 0;
  bool _adopted = false;

  friend class D3D12GPU;
};

}  // namespace tgfx
