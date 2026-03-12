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
#include <Unknwn.h>

namespace tgfx {

namespace {
void** GetVtable(void* comObj) {
  return *reinterpret_cast<void***>(comObj);
}

typedef void(__stdcall* GetDescFn)(IUnknown* self, D3D11Texture2DDesc* pDesc);
typedef void(__stdcall* GetDeviceFn)(IUnknown* self, void** ppDevice);
}  // namespace

bool D3D11GetTextureDesc(void* texture, D3D11Texture2DDesc* outDesc) {
  if (!texture || !outDesc) {
    return false;
  }
  auto getDesc = reinterpret_cast<GetDescFn>(GetVtable(texture)[kD3D11GetDescVtable]);
  getDesc(static_cast<IUnknown*>(texture), outDesc);
  return outDesc->width > 0 && outDesc->height > 0;
}

void* D3D11GetDeviceFromTexture(void* texture) {
  if (!texture) {
    return nullptr;
  }
  auto* unk = static_cast<IUnknown*>(texture);
  auto getDevice = reinterpret_cast<GetDeviceFn>(GetVtable(unk)[kD3D11GetDeviceVtable]);
  void* device = nullptr;
  getDevice(unk, &device);
  // GetDevice adds a reference; release it immediately to keep the ref-count balanced.
  if (device) {
    static_cast<IUnknown*>(device)->Release();
  }
  return device;
}

}  // namespace tgfx
