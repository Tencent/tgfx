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

#include "D3D12HardwareTexture.h"
#include <dxgi1_2.h>
#include "D3D12Defines.h"
#include "D3D12GPU.h"
#include "core/utils/Log.h"

namespace tgfx {

// Opens the given ID3D11Texture2D as a D3D12 resource through the shared-handle path. Returns
// nullptr if the texture is not shareable via NT handle.
static ComPtr<ID3D12Resource> OpenSharedD3D11Texture(ID3D12Device* d3d12Device,
                                                     HardwareBufferRef hardwareBuffer) {
  auto* textureUnk = reinterpret_cast<IUnknown*>(hardwareBuffer);
  ComPtr<IDXGIResource1> dxgiResource1 = nullptr;
  auto qiHr = textureUnk->QueryInterface(IID_PPV_ARGS(&dxgiResource1));
  if (FAILED(qiHr) || !dxgiResource1) {
    return nullptr;
  }
  HANDLE ntHandle = nullptr;
  auto hr =
      dxgiResource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &ntHandle);
  if (FAILED(hr) || ntHandle == nullptr) {
    return nullptr;
  }
  ComPtr<ID3D12Resource> resource = nullptr;
  auto openHr = d3d12Device->OpenSharedHandle(ntHandle, IID_PPV_ARGS(&resource));
  CloseHandle(ntHandle);
  if (FAILED(openHr) || !resource) {
    LOGE("D3D12HardwareTexture: OpenSharedHandle failed, HRESULT=0x%08X",
         static_cast<unsigned>(openHr));
    return nullptr;
  }
  return resource;
}

std::shared_ptr<D3D12HardwareTexture> D3D12HardwareTexture::MakeFrom(
    D3D12GPU* gpu, HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (gpu == nullptr || !HardwareBufferCheck(hardwareBuffer)) {
    return nullptr;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT) {
    LOGE(
        "D3D12HardwareTexture::MakeFrom: RENDER_ATTACHMENT usage is not supported for imported "
        "D3D11 textures.");
    return nullptr;
  }

  auto info = HardwareBufferGetInfo(hardwareBuffer);
  PixelFormat pixelFormat = PixelFormat::Unknown;
  unsigned dxgiFormat = DXGI_FORMAT_UNKNOWN;
  switch (info.format) {
    case HardwareBufferFormat::BGRA_8888:
      pixelFormat = PixelFormat::BGRA_8888;
      dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
      break;
    case HardwareBufferFormat::RGBA_8888:
      pixelFormat = PixelFormat::RGBA_8888;
      dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    case HardwareBufferFormat::ALPHA_8:
      pixelFormat = PixelFormat::ALPHA_8;
      dxgiFormat = DXGI_FORMAT_R8_UNORM;
      break;
    default:
      // NV12 / multi-plane formats are out of scope; callers must lower to BGRA first.
      return nullptr;
  }

  auto resource = OpenSharedD3D11Texture(gpu->device(), hardwareBuffer);
  if (resource == nullptr) {
    LOGE(
        "D3D12HardwareTexture::MakeFrom: the source ID3D11Texture2D cannot be opened on D3D12. "
        "Recreate it with D3D11_RESOURCE_MISC_SHARED_NTHANDLE | "
        "D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX.");
    return nullptr;
  }

  // The keyed mutex must be queried from the D3D11 side: OpenSharedHandle reopens the underlying
  // memory but does not re-expose the DXGI-level IDXGIKeyedMutex object.
  auto* textureUnk = reinterpret_cast<IUnknown*>(hardwareBuffer);
  ComPtr<IDXGIKeyedMutex> keyedMutex = nullptr;
  auto qiHr = textureUnk->QueryInterface(IID_PPV_ARGS(&keyedMutex));
  if (FAILED(qiHr) || keyedMutex == nullptr) {
    LOGE(
        "D3D12HardwareTexture::MakeFrom: shared texture does not expose IDXGIKeyedMutex; "
        "MISC_SHARED_KEYEDMUTEX is required alongside MISC_SHARED_NTHANDLE.");
    return nullptr;
  }

  TextureDescriptor descriptor(info.width, info.height, pixelFormat, /*mipmapped=*/false,
                               /*sampleCount=*/1, usage);
  return gpu->makeResource<D3D12HardwareTexture>(descriptor, std::move(resource), dxgiFormat,
                                                 std::move(keyedMutex), hardwareBuffer);
}

D3D12HardwareTexture::D3D12HardwareTexture(const TextureDescriptor& descriptor,
                                           ComPtr<ID3D12Resource> resource, unsigned dxgiFormat,
                                           ComPtr<IDXGIKeyedMutex> keyedMutex,
                                           HardwareBufferRef hardwareBuffer)
    : D3D12Texture(descriptor, std::move(resource), dxgiFormat), hardwareBuffer(hardwareBuffer),
      _keyedMutex(std::move(keyedMutex)) {
  HardwareBufferRetain(this->hardwareBuffer);
}

D3D12HardwareTexture::~D3D12HardwareTexture() {
  // Balances the retain in the constructor when the object is destroyed without going through
  // onReleaseTexture (which nulls hardwareBuffer after releasing it).
  if (hardwareBuffer != nullptr) {
    HardwareBufferRelease(hardwareBuffer);
    hardwareBuffer = nullptr;
  }
}

void D3D12HardwareTexture::onReleaseTexture() {
  D3D12Texture::onReleaseTexture();
  if (hardwareBuffer != nullptr) {
    HardwareBufferRelease(hardwareBuffer);
    hardwareBuffer = nullptr;
  }
}

}  // namespace tgfx
