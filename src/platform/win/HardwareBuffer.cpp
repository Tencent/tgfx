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

// ID3D11Texture2D vtable layout:
// IUnknown (3) + ID3D11DeviceChild (4: GetDevice, GetPrivateData, SetPrivateData,
//   SetPrivateDataInterface) + ID3D11Resource (2: GetType, SetEvictionPriority, GetEvictionPriority)
//   Actually: ID3D11Resource adds GetType(1) + SetEvictionPriority(1) + GetEvictionPriority(1) = 3
// + ID3D11Texture2D: GetDesc(1) = index 10
// IUnknown: 0=QI, 1=AddRef, 2=Release
// ID3D11DeviceChild: 3=GetDevice, 4=GetPrivateData, 5=SetPrivateData, 6=SetPrivateDataInterface
// ID3D11Resource: 7=GetType, 8=SetEvictionPriority, 9=GetEvictionPriority
// ID3D11Texture2D: 10=GetDesc
constexpr int kGetDescVtableIndex = 10;

typedef void(__stdcall* GetDescFn)(IUnknown* self, D3D11_TEXTURE2D_DESC_MINIMAL* pDesc);

bool GetTextureDesc(void* texture, D3D11_TEXTURE2D_DESC_MINIMAL* outDesc) {
  if (!texture || !outDesc) return false;
  auto* unk = static_cast<IUnknown*>(texture);
  auto vtable = *reinterpret_cast<void***>(unk);
  auto getDesc = reinterpret_cast<GetDescFn>(vtable[kGetDescVtableIndex]);
  getDesc(unk, outDesc);
  return outDesc->Width > 0 && outDesc->Height > 0;
}
}  // namespace

namespace tgfx {

bool HardwareBufferCheck(HardwareBufferRef buffer) {
  if (!buffer) return false;
  D3D11_TEXTURE2D_DESC_MINIMAL desc = {};
  if (!GetTextureDesc(buffer, &desc)) return false;
  // Verify it's a known format
  return desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
         desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
         desc.Format == DXGI_FORMAT_NV12 ||
         desc.Format == DXGI_FORMAT_A8_UNORM;
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

void* HardwareBufferLock(HardwareBufferRef) {
  // D3D11 default/shared textures cannot be mapped directly.
  // CPU access requires a staging texture copy, which is not supported here.
  return nullptr;
}

void HardwareBufferUnlock(HardwareBufferRef) {
}

HardwareBufferInfo HardwareBufferGetInfo(HardwareBufferRef buffer) {
  if (!buffer) return {};
  D3D11_TEXTURE2D_DESC_MINIMAL desc = {};
  if (!GetTextureDesc(buffer, &desc)) return {};

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
