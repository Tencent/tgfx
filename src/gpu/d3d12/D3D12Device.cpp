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
#if !defined(NDEBUG) || defined(TGFX_D3D12_DEBUG_LAYER)
  // Enable the D3D12 debug layer when explicitly requested. Must be called before
  // D3D12CreateDevice. Validation messages surface as readable text instead of
  // generic E_INVALIDARG return codes.
  {
    ComPtr<ID3D12Debug> debugController = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
      debugController->EnableDebugLayer();
    }
  }
#endif
#if !defined(NDEBUG) || defined(TGFX_D3D12_DEBUG_LAYER) || defined(TGFX_D3D12_DRED)
  // Enable Device Removed Extended Data so that, on a TDR/hang, we can ask the driver which
  // command was the last one the GPU started and which one it was about to execute next. This
  // is the cheapest way to localise a hang without attaching PIX. Must be requested before
  // D3D12CreateDevice; queried later via D3D12GPU when GetDeviceRemovedReason() reports a fault.
  {
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings = nullptr;
    auto dredHr = D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings));
    if (SUCCEEDED(dredHr)) {
      dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
      dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
      LOGE("[DRED setup] Auto-breadcrumbs and page-fault tracking enabled.");
    } else {
      LOGE("[DRED setup] D3D12GetDebugInterface(ID3D12DeviceRemovedExtendedDataSettings) "
           "returned HRESULT=0x%08X; DRED unavailable.",
           static_cast<unsigned>(dredHr));
    }
  }
#endif
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
#if !defined(NDEBUG) || defined(TGFX_D3D12_DEBUG_LAYER)
  // Configure the debug-layer info queue so subsequent CreateDescriptorHeap / ResourceBarrier /
  // DrawX failures get logged with the underlying validation message instead of just an opaque
  // HRESULT. Messages are drained per-submission by D3D12GPU::executeSubmission().
  ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
  if (SUCCEEDED(devicePtr->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
    infoQueue->SetMuteDebugOutput(FALSE);
    // Default storage limit is 1024; expand it so we don't lose messages between drains.
    infoQueue->SetMessageCountLimit(8192);
    // Break into the debugger on the most useful severities. When no debugger is attached the
    // breaks are no-ops, but the messages still queue up for DrainDebugMessages() to log.
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
  }
#endif
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
  // The base Device::lockContext() acquires the device mutex before calling us. If the GPU has
  // been removed (e.g. DXGI_ERROR_DEVICE_REMOVED on a previous Signal), every subsequent
  // operation on the device would either return failure immediately or, worse, leave the
  // application waiting on a mutex it owns from a path that already encountered the loss but
  // did not unwind cleanly. We surface the loss here so the base class unlocks the mutex and
  // returns nullptr, matching the OpenGL backend's CONTEXT_LOST handling.
  auto* gpu = static_cast<D3D12GPU*>(_gpu);
  if (gpu->isContextLost()) {
    return false;
  }
  return true;
}

void D3D12Device::onUnlockContext() {
}

}  // namespace tgfx
