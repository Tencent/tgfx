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

#include "D3D11Util.h"

namespace tgfx {

namespace {
void** GetVtable(void* comObj) {
  return *reinterpret_cast<void***>(comObj);
}

typedef void(__stdcall* AddRefFn)(void* self);
typedef void(__stdcall* ReleaseFn)(void* self);
typedef void(__stdcall* GetDescFn)(void* self, D3D11Texture2DDesc* pDesc);
typedef void(__stdcall* GetDeviceFn)(void* self, void** ppDevice);
}  // namespace

void D3D11AddRef(HardwareBufferRef texture) {
  if (!texture) {
    return;
  }
  reinterpret_cast<AddRefFn>(GetVtable(texture)[1])(texture);
}

void D3D11Release(HardwareBufferRef texture) {
  if (!texture) {
    return;
  }
  reinterpret_cast<ReleaseFn>(GetVtable(texture)[2])(texture);
}

bool D3D11GetTextureDesc(HardwareBufferRef texture, D3D11Texture2DDesc* outDesc) {
  if (!texture || !outDesc) {
    return false;
  }
  reinterpret_cast<GetDescFn>(GetVtable(texture)[kD3D11GetDescVtable])(texture, outDesc);
  return outDesc->width > 0 && outDesc->height > 0;
}

ID3D11Device* D3D11GetDeviceFromTexture(HardwareBufferRef texture) {
  if (!texture) {
    return nullptr;
  }
  void* device = nullptr;
  reinterpret_cast<GetDeviceFn>(GetVtable(texture)[kD3D11GetDeviceVtable])(texture, &device);
  // GetDevice adds a reference; release it immediately to keep the ref-count balanced.
  if (device) {
    reinterpret_cast<ReleaseFn>(GetVtable(device)[2])(device);
  }
  return static_cast<ID3D11Device*>(device);
}

}  // namespace tgfx
