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

#include "D3D12Texture.h"
#include "D3D12Defines.h"
#include "D3D12GPU.h"
#include "core/utils/Log.h"

namespace tgfx {

static D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(uint32_t usage) {
  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
  if (usage & TextureUsage::RENDER_ATTACHMENT) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }
  return flags;
}

std::shared_ptr<D3D12Texture> D3D12Texture::Make(D3D12GPU* gpu,
                                                 const TextureDescriptor& descriptor) {
  if (gpu == nullptr || descriptor.width <= 0 || descriptor.height <= 0) {
    return nullptr;
  }

  // D3D12 disallows D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS on MSAA resources, and we need
  // UAV access on every mipmapped texture so the compute mipmap generator can write each
  // downsampled level. Rather than create the resource and let CreateCommittedResource fail
  // with a runtime debug-layer error, reject the combination up front. The GL backend already
  // enforces the same contract in GLMultisampleTexture::MakeFrom; Metal's MTLTextureType2D-
  // Multisample type cannot carry mip levels at all and so doesn't need an explicit check.
  if (descriptor.mipLevelCount > 1 && descriptor.sampleCount > 1) {
    LOGE("D3D12Texture::Make() multisample textures cannot have mip levels (mipLevelCount=%d, "
         "sampleCount=%d).",
         descriptor.mipLevelCount, descriptor.sampleCount);
    return nullptr;
  }

  auto dxgiFormat = static_cast<DXGI_FORMAT>(gpu->getDXGIFormat(descriptor.format));
  if (dxgiFormat == static_cast<DXGI_FORMAT>(DXGI_FORMAT_UNKNOWN)) {
    LOGE("D3D12Texture::Make() unsupported pixel format: %d", static_cast<int>(descriptor.format));
    return nullptr;
  }

  bool isDepthStencil = (descriptor.format == PixelFormat::DEPTH24_STENCIL8);

  D3D12_RESOURCE_FLAGS resourceFlags = ToD3D12ResourceFlags(descriptor.usage);
  if (isDepthStencil) {
    resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    resourceFlags &= ~D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  } else {
    // D3D12 requires ALLOW_RENDER_TARGET to be set at resource creation time before any
    // CreateRenderTargetView call against the resource is legal. Other backends (Vulkan/Metal)
    // can derive render-target capability lazily, so callers across the codebase commonly
    // create textures with the default usage (TEXTURE_BINDING) and later wrap them via
    // Surface::MakeFrom(context, backendTexture, ...). To keep that path working on D3D12 we
    // unconditionally enable the flag for any non-depth, renderable colour format. The cost is
    // marginal (some drivers skip a sampling-only compression path) and it avoids hard
    // device-removal when a sampled texture is later asked to act as a render target.
    if (gpu->isFormatRenderable(descriptor.format)) {
      resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (descriptor.mipLevelCount > 1) {
      // Mipmapped textures need to be writable from a compute shader so that
      // generateMipmapsForTexture() can downsample mip[i] -> mip[i+1] via UAV writes. The flag is
      // a no-op for the basic sampling path and only adds a small driver-internal alignment
      // overhead.
      resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
  }

  D3D12_HEAP_PROPERTIES heapProperties = {};
  heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC resourceDesc = {};
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resourceDesc.Width = static_cast<UINT64>(descriptor.width);
  resourceDesc.Height = static_cast<UINT>(descriptor.height);
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.MipLevels = static_cast<UINT16>(descriptor.mipLevelCount);
  resourceDesc.Format = dxgiFormat;
  resourceDesc.SampleDesc.Count = static_cast<UINT>(descriptor.sampleCount);
  resourceDesc.SampleDesc.Quality = 0;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resourceDesc.Flags = resourceFlags;

  // Optimised clear values let D3D12 fast-path ClearRenderTargetView / ClearDepthStencilView
  // when the runtime-supplied clear matches. We don't know the clear colour at creation time
  // (callers vary, e.g. RGBA transparent for offscreen surfaces, white for blur seed), so for
  // colour render targets we pass nullptr — forcing the slow-but-deterministic clear path is
  // preferable to a perpetual "clear values do not match" debug-layer warning that some drivers
  // also turn into a stalled GPU clear. Depth-stencil keeps an optimised value because the test
  // suite uses a single canonical (0.0 depth, 0 stencil) clear.
  D3D12_CLEAR_VALUE* clearValue = nullptr;
  D3D12_CLEAR_VALUE clearValueStorage = {};
  if (isDepthStencil) {
    clearValueStorage.Format = dxgiFormat;
    clearValueStorage.DepthStencil.Depth = 0.0f;
    clearValueStorage.DepthStencil.Stencil = 0;
    clearValue = &clearValueStorage;
  }

  D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

  ComPtr<ID3D12Resource> d3d12Resource = nullptr;
  auto hr = gpu->device()->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                   &resourceDesc, initialState, clearValue,
                                                   IID_PPV_ARGS(&d3d12Resource));
  if (FAILED(hr)) {
    LOGE("D3D12Texture::Make() CreateCommittedResource failed, HRESULT=0x%08X",
         static_cast<unsigned>(hr));
    return nullptr;
  }

  return gpu->makeResource<D3D12Texture>(descriptor, std::move(d3d12Resource),
                                         static_cast<unsigned>(dxgiFormat));
}

std::shared_ptr<D3D12Texture> D3D12Texture::MakeFrom(D3D12GPU* gpu, ComPtr<ID3D12Resource> resource,
                                                     unsigned dxgiFormat, uint32_t usage,
                                                     bool /*adopted*/) {
  if (gpu == nullptr || resource == nullptr) {
    return nullptr;
  }

  auto desc = resource->GetDesc();
  TextureDescriptor descriptor = {};
  descriptor.width = static_cast<int>(desc.Width);
  descriptor.height = static_cast<int>(desc.Height);
  descriptor.format = DXGIFormatToPixelFormat(dxgiFormat);
  descriptor.mipLevelCount = static_cast<int>(desc.MipLevels);
  descriptor.sampleCount = static_cast<int>(desc.SampleDesc.Count);
  descriptor.usage = usage;
  // The `adopted` flag is intentionally ignored on D3D12: COM reference counting makes the
  // distinction Vulkan/Metal draw — "tgfx must explicitly destroy" vs "caller keeps owning it" —
  // meaningless here. ComPtr<ID3D12Resource> always carries its own AddRef/Release pair, so:
  //   * adopted == true  : caller hands its reference to us; on D3D12Texture destruction the
  //     ComPtr Release brings the refcount to zero and the runtime destroys the resource.
  //   * adopted == false : caller keeps its reference; we hold an additional one. The resource
  //     stays alive at least until both refs are released, satisfying the GPU::importBackendTexture
  //     contract that the backend texture remain valid for the wrapped Texture's lifetime.
  // Either way the cleanup logic is identical, so a single code path is enough.
  return gpu->makeResource<D3D12Texture>(descriptor, std::move(resource), dxgiFormat);
}

D3D12Texture::D3D12Texture(const TextureDescriptor& descriptor,
                           ComPtr<ID3D12Resource> d3d12Resource, unsigned dxgiFormat)
    : Texture(descriptor), resource(std::move(d3d12Resource)), _dxgiFormat(dxgiFormat) {
}

void D3D12Texture::onRelease(D3D12GPU*) {
  onReleaseTexture();
}

void D3D12Texture::onReleaseTexture() {
  resource = nullptr;
}

BackendTexture D3D12Texture::getBackendTexture() const {
  if (resource == nullptr || !(descriptor.usage & TextureUsage::TEXTURE_BINDING)) {
    return {};
  }
  D3D12TextureInfo d3d12Info = {};
  d3d12Info.resource = resource.Get();
  d3d12Info.format = _dxgiFormat;
  return BackendTexture(d3d12Info, descriptor.width, descriptor.height);
}

BackendRenderTarget D3D12Texture::getBackendRenderTarget() const {
  if (resource == nullptr || !(descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    return {};
  }
  D3D12TextureInfo d3d12Info = {};
  d3d12Info.resource = resource.Get();
  d3d12Info.format = _dxgiFormat;
  return BackendRenderTarget(d3d12Info, descriptor.width, descriptor.height);
}

}  // namespace tgfx
