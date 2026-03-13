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

#include <Unknwn.h>

// Minimal DXGI format constants to avoid including dxgiformat.h.
#ifndef DXGI_FORMAT_R8G8B8A8_UNORM
#define DXGI_FORMAT_R8G8B8A8_UNORM 28
#endif
#ifndef DXGI_FORMAT_A8_UNORM
#define DXGI_FORMAT_A8_UNORM 65
#endif
#ifndef DXGI_FORMAT_B8G8R8A8_UNORM
#define DXGI_FORMAT_B8G8R8A8_UNORM 87
#endif
#ifndef DXGI_FORMAT_NV12
#define DXGI_FORMAT_NV12 103
#endif

// Minimal D3D11 constants to avoid including d3d11.h.
#ifndef D3D11_USAGE_STAGING
#define D3D11_USAGE_STAGING 3
#endif
#ifndef D3D11_CPU_ACCESS_READ
#define D3D11_CPU_ACCESS_READ 0x20000
#endif
#ifndef D3D11_MAP_READ
#define D3D11_MAP_READ 1
#endif

namespace tgfx {

// ============================================================================
// Vtable indices for D3D11 COM interfaces (derived from d3d11.idl).
//
// ID3D11Texture2D vtable layout:
//   IUnknown: 0=QI, 1=AddRef, 2=Release
//   ID3D11DeviceChild: 3=GetDevice, 4=GetPrivateData, 5=SetPrivateData, 6=SetPrivateDataInterface
//   ID3D11Resource: 7=GetType, 8=SetEvictionPriority, 9=GetEvictionPriority
//   ID3D11Texture2D: 10=GetDesc
//
// ID3D11Device vtable layout:
//   IUnknown: 0=QI, 1=AddRef, 2=Release
//   3=CreateBuffer, 4=CreateTexture1D, 5=CreateTexture2D, 6=CreateTexture3D,
//   7=CreateShaderResourceView, ..., 40=GetImmediateContext
//
// ID3D11DeviceContext vtable layout:
//   IUnknown: 0=QI, 1=AddRef, 2=Release
//   ID3D11DeviceChild: 3=GetDevice, ...
//   14=Map, 15=Unmap, ..., 47=CopyResource
// ============================================================================
constexpr int kD3D11GetDeviceVtable = 3;
constexpr int kD3D11GetDescVtable = 10;
constexpr int kD3D11CreateTex2DVtable = 5;
constexpr int kD3D11GetImmCtxVtable = 40;
constexpr int kD3D11MapVtable = 14;
constexpr int kD3D11UnmapVtable = 15;
constexpr int kD3D11CopyResVtable = 47;

// Minimal equivalent of D3D11_TEXTURE2D_DESC.
struct D3D11Texture2DDesc {
  unsigned int width = 0;
  unsigned int height = 0;
  unsigned int mipLevels = 0;
  unsigned int arraySize = 0;
  unsigned int format = 0;  // DXGI_FORMAT
  struct {
    unsigned int count = 0;
    unsigned int quality = 0;
  } sampleDesc;
  unsigned int usage = 0;
  unsigned int bindFlags = 0;
  unsigned int cpuAccessFlags = 0;
  unsigned int miscFlags = 0;
};

// Minimal equivalent of D3D11_MAPPED_SUBRESOURCE.
struct D3D11MappedSubresource {
  void* data = nullptr;
  unsigned int rowPitch = 0;
  unsigned int depthPitch = 0;
};

/**
 * Retrieves the texture descriptor from an ID3D11Texture2D pointer via vtable call.
 * Returns false if texture is null or the dimensions are zero.
 */
bool D3D11GetTextureDesc(IUnknown* texture, D3D11Texture2DDesc* outDesc);

/**
 * Retrieves the ID3D11Device that owns the given ID3D11Texture2D. The returned pointer does not
 * carry an extra reference (the reference added by GetDevice is immediately released).
 */
IUnknown* D3D11GetDeviceFromTexture(IUnknown* texture);

}  // namespace tgfx
