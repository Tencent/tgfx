/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/platform/HardwareBuffer.h"
#include <mutex>
#include <unordered_map>
#include "D3D11Util.h"

namespace tgfx {

// ============================================================================
// Vtable function typedefs for ID3D11Device and ID3D11DeviceContext.
// These interfaces are accessed via raw vtable dispatch to avoid including d3d11.h.
// ============================================================================
typedef void(__stdcall* ReleaseFn)(void* self);
typedef void(__stdcall* GetImmediateContextFn)(void* self, void** ppContext);
typedef long(__stdcall* CreateTexture2DFn)(void* self, const D3D11Texture2DDesc* pDesc,
                                           const void* pInitialData, void** ppTexture2D);
typedef long(__stdcall* MapFn)(void* self, void* pResource, unsigned int Subresource,
                               unsigned int MapType, unsigned int MapFlags,
                               D3D11MappedSubresource* pMappedResource);
typedef void(__stdcall* UnmapFn)(void* self, void* pResource, unsigned int Subresource);
typedef void(__stdcall* CopyResourceFn)(void* self, void* pDst, void* pSrc);

static void** GetVtable(void* comObj) {
  return *reinterpret_cast<void***>(comObj);
}

static void ComRelease(void* comObj) {
  reinterpret_cast<ReleaseFn>(GetVtable(comObj)[2])(comObj);
}

// ============================================================================
// Lock state management
// ============================================================================
struct LockState {
  void* stagingTexture = nullptr;
  void* context = nullptr;
};

static std::mutex g_lockMutex;
static std::unordered_map<HardwareBufferRef, LockState> g_lockStates;

bool HardwareBufferCheck(HardwareBufferRef buffer) {
  if (!buffer) {
    return false;
  }
  D3D11Texture2DDesc desc = {};
  if (!D3D11GetTextureDesc(buffer, &desc)) {
    return false;
  }
  return desc.format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.format == DXGI_FORMAT_R8G8B8A8_UNORM ||
         desc.format == DXGI_FORMAT_NV12 || desc.format == DXGI_FORMAT_A8_UNORM;
}

HardwareBufferRef HardwareBufferAllocate(int, int, bool) {
  // D3D11 textures must be created via ID3D11Device::CreateTexture2D externally.
  return nullptr;
}

HardwareBufferRef HardwareBufferRetain(HardwareBufferRef buffer) {
  D3D11AddRef(buffer);
  return buffer;
}

void HardwareBufferRelease(HardwareBufferRef buffer) {
  D3D11Release(buffer);
}

void* HardwareBufferLock(HardwareBufferRef buffer) {
  if (!buffer) {
    return nullptr;
  }
  D3D11Texture2DDesc desc = {};
  if (!D3D11GetTextureDesc(buffer, &desc)) {
    return nullptr;
  }
  // NV12 is a multi-planar format; its CPU data cannot be represented as a single base address.
  // NV12 buffers are consumed via the GPU texture path (YUVTextureView) and should never reach here.
  if (desc.format == DXGI_FORMAT_NV12) {
    return nullptr;
  }

  // Step 1: Get ID3D11Device from the texture. D3D11GetDeviceFromTexture returns a non-owning
  // pointer; the texture itself holds a reference to the device for the duration of this call.
  ID3D11Device* device = D3D11GetDeviceFromTexture(buffer);
  if (!device) {
    return nullptr;
  }

  // Step 2: Get immediate context from device
  void* context = nullptr;
  reinterpret_cast<GetImmediateContextFn>(GetVtable(device)[D3D11GetImmCtxVtable])(device,
                                                                                   &context);
  if (!context) {
    return nullptr;
  }

  // Step 3: Create a staging texture with CPU read access
  D3D11Texture2DDesc stagingDesc = desc;
  stagingDesc.mipLevels = 1;
  stagingDesc.arraySize = 1;
  stagingDesc.sampleDesc.count = 1;
  stagingDesc.sampleDesc.quality = 0;
  stagingDesc.usage = D3D11_USAGE_STAGING;
  stagingDesc.bindFlags = 0;
  stagingDesc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  stagingDesc.miscFlags = 0;

  void* stagingTexture = nullptr;
  long hr = reinterpret_cast<CreateTexture2DFn>(GetVtable(device)[D3D11CreateTex2DVtable])(
      device, &stagingDesc, nullptr, &stagingTexture);
  if (hr < 0 || !stagingTexture) {
    ComRelease(context);
    return nullptr;
  }

  // Step 4: Copy the source texture to the staging texture
  reinterpret_cast<CopyResourceFn>(GetVtable(context)[D3D11CopyResVtable])(context, stagingTexture,
                                                                           buffer);

  // Step 5: Map the staging texture for CPU read
  D3D11MappedSubresource mapped = {};
  hr = reinterpret_cast<MapFn>(GetVtable(context)[D3D11MapVtable])(context, stagingTexture, 0,
                                                                   D3D11_MAP_READ, 0, &mapped);
  if (hr < 0 || !mapped.data) {
    ComRelease(stagingTexture);
    ComRelease(context);
    return nullptr;
  }

  // Store the lock state for Unlock
  {
    std::lock_guard<std::mutex> lock(g_lockMutex);
    g_lockStates[buffer] = {stagingTexture, context};
  }
  return mapped.data;
}

void HardwareBufferUnlock(HardwareBufferRef buffer) {
  if (!buffer) {
    return;
  }
  LockState state = {};
  {
    std::lock_guard<std::mutex> lock(g_lockMutex);
    auto it = g_lockStates.find(buffer);
    if (it == g_lockStates.end()) {
      return;
    }
    state = it->second;
    g_lockStates.erase(it);
  }

  // Unmap the staging texture
  reinterpret_cast<UnmapFn>(GetVtable(state.context)[D3D11UnmapVtable])(state.context,
                                                                        state.stagingTexture, 0);

  // Release staging texture and context
  ComRelease(state.stagingTexture);
  ComRelease(state.context);
}

HardwareBufferInfo HardwareBufferGetInfo(HardwareBufferRef buffer) {
  if (!buffer) {
    return {};
  }
  D3D11Texture2DDesc desc = {};
  if (!D3D11GetTextureDesc(buffer, &desc)) {
    return {};
  }

  HardwareBufferInfo info = {};
  info.width = static_cast<int>(desc.width);
  info.height = static_cast<int>(desc.height);

  switch (desc.format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
      info.format = HardwareBufferFormat::BGRA_8888;
      info.rowBytes = static_cast<size_t>(desc.width) * 4;
      break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
      info.format = HardwareBufferFormat::RGBA_8888;
      info.rowBytes = static_cast<size_t>(desc.width) * 4;
      break;
    case DXGI_FORMAT_NV12:
      info.format = HardwareBufferFormat::YCBCR_420_SP;
      info.rowBytes = static_cast<size_t>(desc.width);
      break;
    case DXGI_FORMAT_A8_UNORM:
      info.format = HardwareBufferFormat::ALPHA_8;
      info.rowBytes = static_cast<size_t>(desc.width);
      break;
    default:
      return {};
  }
  return info;
}

}  // namespace tgfx
