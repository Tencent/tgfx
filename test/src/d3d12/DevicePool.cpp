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

#include "utils/DevicePool.h"
#include "tgfx/gpu/d3d12/D3D12Device.h"

namespace tgfx {
thread_local std::shared_ptr<Device> cachedDevice = nullptr;

std::shared_ptr<Device> DevicePool::Make() {
  if (cachedDevice == nullptr) {
#ifdef TGFX_D3D12_USE_WARP
    // CI opt-in (-DTGFX_D3D12_USE_WARP=ON): force the test suite onto WARP so headless runners
    // without a hardware adapter can still exercise the D3D12 backend. WARP is functionally
    // complete but very slow — never enable this for performance baselines.
    cachedDevice = D3D12Device::MakeWarp();
#else
    cachedDevice = D3D12Device::Make();
#endif
  }
  return cachedDevice;
}
}  // namespace tgfx
