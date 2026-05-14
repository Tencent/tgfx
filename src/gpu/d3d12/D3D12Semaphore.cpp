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

#include "D3D12Semaphore.h"
#include "D3D12GPU.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<D3D12Semaphore> D3D12Semaphore::Make(D3D12GPU* gpu) {
  if (gpu == nullptr) {
    return nullptr;
  }
  ComPtr<ID3D12Fence> fence = nullptr;
  auto hr = gpu->device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  if (FAILED(hr)) {
    LOGE("D3D12Semaphore::Make() CreateFence failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
    return nullptr;
  }
  return gpu->makeResource<D3D12Semaphore>(std::move(fence), static_cast<uint64_t>(0), false);
}

std::shared_ptr<D3D12Semaphore> D3D12Semaphore::MakeFrom(D3D12GPU* gpu, ComPtr<ID3D12Fence> fence,
                                                        uint64_t value) {
  if (gpu == nullptr || fence == nullptr) {
    return nullptr;
  }
  return gpu->makeResource<D3D12Semaphore>(std::move(fence), value, true);
}

D3D12Semaphore::D3D12Semaphore(ComPtr<ID3D12Fence> fence, uint64_t value, bool adopted)
    : _fence(std::move(fence)), _value(value), _adopted(adopted) {
}

BackendSemaphore D3D12Semaphore::getBackendSemaphore() const {
  if (_fence == nullptr) {
    return {};
  }
  D3D12SyncInfo info = {};
  info.fence = _fence.Get();
  info.value = _value;
  return BackendSemaphore(info);
}

void D3D12Semaphore::onRelease(D3D12GPU*) {
  if (!_adopted) {
    _fence = nullptr;
  }
}

}  // namespace tgfx
