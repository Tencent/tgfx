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

#include "D3D12Device.h"
#include <dxgi1_4.h>
#include "D3D12GPU.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<D3D12Device> D3D12Device::Make() {
  ComPtr<IDXGIFactory4> factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    LOGE("D3D12Device::Make() Failed to create DXGI factory.");
    return nullptr;
  }
  ComPtr<IDXGIAdapter1> adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc = {};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      adapter = nullptr;
      continue;
    }
    ComPtr<ID3D12Device> d3d12Device = nullptr;
    if (SUCCEEDED(
            D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device)))) {
      return MakeFrom(d3d12Device.Get());
    }
    adapter = nullptr;
  }
  LOGE("D3D12Device::Make() No suitable D3D12 hardware adapter found.");
  return nullptr;
}

std::shared_ptr<D3D12Device> D3D12Device::MakeFrom(void* device) {
  if (device == nullptr) {
    return nullptr;
  }
  auto d3d12Device = static_cast<ID3D12Device*>(device);
  ComPtr<ID3D12Device> devicePtr = nullptr;
  d3d12Device->QueryInterface(IID_PPV_ARGS(&devicePtr));
  if (devicePtr == nullptr) {
    return nullptr;
  }
  auto gpu = D3D12GPU::Make(std::move(devicePtr));
  if (gpu == nullptr) {
    return nullptr;
  }
  auto result = std::shared_ptr<D3D12Device>(new D3D12Device(std::move(gpu)));
  result->weakThis = result;
  return result;
}

D3D12Device::D3D12Device(std::unique_ptr<D3D12GPU> gpu) : Device(std::move(gpu)) {
}

D3D12Device::~D3D12Device() {
  static_cast<D3D12GPU*>(_gpu)->releaseAll(true);
}

void* D3D12Device::d3d12Device() const {
  return static_cast<D3D12GPU*>(_gpu)->device();
}

bool D3D12Device::onLockContext() {
  return true;
}

void D3D12Device::onUnlockContext() {
}

}  // namespace tgfx
