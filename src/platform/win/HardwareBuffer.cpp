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
#include <Unknwn.h>
#include <mutex>
#include <unordered_map>

// Minimal DXGI format constants to avoid #include <dxgiformat.h>
#ifndef DXGI_FORMAT_B8G8R8A8_UNORM
#define DXGI_FORMAT_B8G8R8A8_UNORM 87
#endif
#ifndef DXGI_FORMAT_R8G8B8A8_UNORM
#define DXGI_FORMAT_R8G8B8A8_UNORM 28
#endif
#ifndef DXGI_FORMAT_NV12
#define DXGI_FORMAT_NV12 103
#endif
#ifndef DXGI_FORMAT_A8_UNORM
#define DXGI_FORMAT_A8_UNORM 65
#endif

// Minimal D3D11 constants to avoid #include <d3d11.h>
#ifndef D3D11_USAGE_STAGING
#define D3D11_USAGE_STAGING 3
#endif
#ifndef D3D11_CPU_ACCESS_READ
#define D3D11_CPU_ACCESS_READ 0x20000
#endif
#ifndef D3D11_MAP_READ
#define D3D11_MAP_READ 1
#endif

// Minimal D3D11 texture descriptor to avoid #include <d3d11.h>
// ID3D11Texture2D::GetDesc outputs this structure.
namespace {
struct D3D11_TEXTURE2D_DESC_MINIMAL {
  unsigned int Width;
  unsigned int Height;
  unsigned int MipLevels;
  unsigned int ArraySize;
  unsigned int Format;  // DXGI_FORMAT
  struct {
    unsigned int Count;
    unsigned int Quality;
  } SampleDesc;
  unsigned int Usage;
  unsigned int BindFlags;
  unsigned int CPUAccessFlags;
  unsigned int MiscFlags;
};

// D3D11_MAPPED_SUBRESOURCE equivalent
struct D3D11_MAPPED_SUBRESOURCE_MINIMAL {
  void* pData;
  unsigned int RowPitch;
  unsigned int DepthPitch;
};

// ============================================================================
// Vtable indices for D3D11 COM interfaces (derived from d3d11.idl)
// ============================================================================

// ID3D11Texture2D vtable layout:
// IUnknown: 0=QI, 1=AddRef, 2=Release
// ID3D11DeviceChild: 3=GetDevice, 4=GetPrivateData, 5=SetPrivateData, 6=SetPrivateDataInterface
// ID3D11Resource: 7=GetType, 8=SetEvictionPriority, 9=GetEvictionPriority
// ID3D11Texture2D: 10=GetDesc
constexpr int kGetDescVtableIndex = 10;
// ID3D11DeviceChild::GetDevice = index 3
constexpr int kGetDeviceVtableIndex = 3;

// ID3D11Device vtable layout:
// IUnknown: 0=QI, 1=AddRef, 2=Release
// 3=CreateBuffer, 4=CreateTexture1D, 5=CreateTexture2D, ...
// 40=GetImmediateContext
constexpr int kCreateTexture2DVtableIndex = 5;
constexpr int kGetImmediateContextVtableIndex = 40;

// ID3D11DeviceContext vtable layout:
// IUnknown: 0=QI, 1=AddRef, 2=Release
// ID3D11DeviceChild: 3=GetDevice, 4=GetPrivateData, 5=SetPrivateData, 6=SetPrivateDataInterface
// 7=VSSetConstantBuffers, ..., 14=Map, 15=Unmap, ..., 47=CopyResource
constexpr int kMapVtableIndex = 14;
constexpr int kUnmapVtableIndex = 15;
constexpr int kCopyResourceVtableIndex = 47;

// ============================================================================
// Vtable function typedefs
// ============================================================================
typedef void(__stdcall* GetDescFn)(IUnknown* self, D3D11_TEXTURE2D_DESC_MINIMAL* pDesc);
typedef void(__stdcall* GetDeviceFn)(IUnknown* self, void** ppDevice);
typedef void(__stdcall* GetImmediateContextFn)(IUnknown* self, void** ppContext);
typedef long(__stdcall* CreateTexture2DFn)(IUnknown* self,
                                           const D3D11_TEXTURE2D_DESC_MINIMAL* pDesc,
                                           const void* pInitialData, void** ppTexture2D);
typedef long(__stdcall* MapFn)(IUnknown* self, IUnknown* pResource, unsigned int Subresource,
                               unsigned int MapType, unsigned int MapFlags,
                               D3D11_MAPPED_SUBRESOURCE_MINIMAL* pMappedResource);
typedef void(__stdcall* UnmapFn)(IUnknown* self, IUnknown* pResource, unsigned int Subresource);
typedef void(__stdcall* CopyResourceFn)(IUnknown* self, IUnknown* pDst, IUnknown* pSrc);

// ============================================================================
// Helper functions
// ============================================================================
static void** GetVtable(void* comObj) {
  return *reinterpret_cast<void***>(comObj);
}

bool GetTextureDesc(void* texture, D3D11_TEXTURE2D_DESC_MINIMAL* outDesc) {
  if (!texture || !outDesc) {
    return false;
  }
  auto getDesc = reinterpret_cast<GetDescFn>(GetVtable(texture)[kGetDescVtableIndex]);
  getDesc(static_cast<IUnknown*>(texture), outDesc);
  return outDesc->Width > 0 && outDesc->Height > 0;
}

// ============================================================================
// Lock state management
// ============================================================================
struct LockState {
  IUnknown* stagingTexture = nullptr;
  IUnknown* context = nullptr;
};

static std::mutex g_lockMutex;
static std::unordered_map<void*, LockState> g_lockStates;
}  // namespace

namespace tgfx {

bool HardwareBufferCheck(HardwareBufferRef buffer) {
  if (!buffer) {
    return false;
  }
  D3D11_TEXTURE2D_DESC_MINIMAL desc = {};
  if (!GetTextureDesc(buffer, &desc)) return false;
  // Verify it's a known format
  return desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
         desc.Format == DXGI_FORMAT_NV12 || desc.Format == DXGI_FORMAT_A8_UNORM;
}

HardwareBufferRef HardwareBufferAllocate(int, int, bool) {
  // D3D11 textures must be created via ID3D11Device::CreateTexture2D externally.
  return nullptr;
}

HardwareBufferRef HardwareBufferRetain(HardwareBufferRef buffer) {
  if (buffer) {
    static_cast<IUnknown*>(buffer)->AddRef();
  }
  return buffer;
}

void HardwareBufferRelease(HardwareBufferRef buffer) {
  if (buffer) {
    static_cast<IUnknown*>(buffer)->Release();
  }
}

void* HardwareBufferLock(HardwareBufferRef buffer) {
  if (!buffer) {
    return nullptr;
  }
  D3D11_TEXTURE2D_DESC_MINIMAL desc = {};
  if (!GetTextureDesc(buffer, &desc)) {
    return nullptr;
  }
  // NV12 is a multi-planar format; its CPU data cannot be represented as a single base address.
  // NV12 buffers are consumed via the GPU texture path (YUVTextureView) and should never reach here.
  if (desc.Format == DXGI_FORMAT_NV12) {
    return nullptr;
  }

  // Step 1: Get ID3D11Device from the texture via ID3D11DeviceChild::GetDevice
  auto* texUnk = static_cast<IUnknown*>(buffer);
  void* device = nullptr;
  auto getDevice = reinterpret_cast<GetDeviceFn>(GetVtable(texUnk)[kGetDeviceVtableIndex]);
  getDevice(texUnk, &device);
  if (!device) {
    return nullptr;
  }
  auto* deviceUnk = static_cast<IUnknown*>(device);

  // Step 2: Get immediate context from device
  void* context = nullptr;
  auto getCtx = reinterpret_cast<GetImmediateContextFn>(
      GetVtable(deviceUnk)[kGetImmediateContextVtableIndex]);
  getCtx(deviceUnk, &context);
  if (!context) {
    deviceUnk->Release();
    return nullptr;
  }

  // Step 3: Create a staging texture with CPU read access
  D3D11_TEXTURE2D_DESC_MINIMAL stagingDesc = desc;
  stagingDesc.MipLevels = 1;
  stagingDesc.ArraySize = 1;
  stagingDesc.SampleDesc.Count = 1;
  stagingDesc.SampleDesc.Quality = 0;
  stagingDesc.Usage = D3D11_USAGE_STAGING;
  stagingDesc.BindFlags = 0;
  stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  stagingDesc.MiscFlags = 0;

  void* stagingTexture = nullptr;
  auto createTex =
      reinterpret_cast<CreateTexture2DFn>(GetVtable(deviceUnk)[kCreateTexture2DVtableIndex]);
  long hr = createTex(deviceUnk, &stagingDesc, nullptr, &stagingTexture);
  deviceUnk->Release();
  if (hr < 0 || !stagingTexture) {
    static_cast<IUnknown*>(context)->Release();
    return nullptr;
  }

  // Step 4: Copy the source texture to the staging texture
  auto copyResource =
      reinterpret_cast<CopyResourceFn>(GetVtable(context)[kCopyResourceVtableIndex]);
  copyResource(static_cast<IUnknown*>(context), static_cast<IUnknown*>(stagingTexture), texUnk);

  // Step 5: Map the staging texture for CPU read
  D3D11_MAPPED_SUBRESOURCE_MINIMAL mapped = {};
  auto map = reinterpret_cast<MapFn>(GetVtable(context)[kMapVtableIndex]);
  hr = map(static_cast<IUnknown*>(context), static_cast<IUnknown*>(stagingTexture), 0,
           D3D11_MAP_READ, 0, &mapped);
  if (hr < 0 || !mapped.pData) {
    static_cast<IUnknown*>(stagingTexture)->Release();
    static_cast<IUnknown*>(context)->Release();
    return nullptr;
  }

  // Store the lock state for Unlock
  {
    std::lock_guard<std::mutex> lock(g_lockMutex);
    g_lockStates[buffer] = {static_cast<IUnknown*>(stagingTexture),
                            static_cast<IUnknown*>(context)};
  }
  return mapped.pData;
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
  auto unmap = reinterpret_cast<UnmapFn>(GetVtable(state.context)[kUnmapVtableIndex]);
  unmap(state.context, state.stagingTexture, 0);

  // Release staging texture and context
  state.stagingTexture->Release();
  state.context->Release();
}

HardwareBufferInfo HardwareBufferGetInfo(HardwareBufferRef buffer) {
  if (!buffer) {
    return {};
  }
  D3D11_TEXTURE2D_DESC_MINIMAL desc = {};
  if (!GetTextureDesc(buffer, &desc)) {
    return {};
  }

  HardwareBufferInfo info = {};
  info.width = static_cast<int>(desc.Width);
  info.height = static_cast<int>(desc.Height);

  switch (desc.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
      info.format = HardwareBufferFormat::BGRA_8888;
      info.rowBytes = static_cast<size_t>(desc.Width) * 4;
      break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
      info.format = HardwareBufferFormat::RGBA_8888;
      info.rowBytes = static_cast<size_t>(desc.Width) * 4;
      break;
    case DXGI_FORMAT_NV12:
      info.format = HardwareBufferFormat::YCBCR_420_SP;
      info.rowBytes = static_cast<size_t>(desc.Width);
      break;
    case DXGI_FORMAT_A8_UNORM:
      info.format = HardwareBufferFormat::ALPHA_8;
      info.rowBytes = static_cast<size_t>(desc.Width);
      break;
    default:
      return {};
  }
  return info;
}
}  // namespace tgfx
