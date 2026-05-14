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

#include "D3D12GPU.h"
#include <dxgi1_4.h>
#include <shaderc/shaderc.hpp>
#include <string>
#include "D3D12Buffer.h"
#include "D3D12CommandQueue.h"
#include "D3D12Resource.h"
#include "D3D12Sampler.h"
#include "D3D12Semaphore.h"
#include "D3D12Texture.h"
#include "core/utils/Log.h"

namespace tgfx {

bool HardwareBufferAvailable() {
  return false;
}

static ComPtr<IDXGIAdapter1> FindAdapter(ID3D12Device* device) {
  ComPtr<IDXGIFactory4> factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return nullptr;
  }
  LUID luid = device->GetAdapterLuid();
  ComPtr<IDXGIAdapter1> adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc = {};
    adapter->GetDesc1(&desc);
    if (desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) {
      return adapter;
    }
    adapter = nullptr;
  }
  return nullptr;
}

std::unique_ptr<D3D12GPU> D3D12GPU::Make(ComPtr<ID3D12Device> device) {
  if (device == nullptr) {
    return nullptr;
  }
  auto adapter = FindAdapter(device.Get());
  auto gpu = std::unique_ptr<D3D12GPU>(new D3D12GPU(std::move(device), std::move(adapter)));
  if (gpu->commandQueue == nullptr) {
    return nullptr;
  }
  return gpu;
}

D3D12GPU::D3D12GPU(ComPtr<ID3D12Device> device, ComPtr<IDXGIAdapter1> adapter)
    : d3d12Device(std::move(device)), dxgiAdapter(std::move(adapter)) {
  initInfo();
  initFeatures();
  initLimits();
  commandQueue = std::make_unique<D3D12CommandQueue>(this);
  compiler = std::make_unique<shaderc::Compiler>();
}

D3D12GPU::~D3D12GPU() {
  DEBUG_ASSERT(returnQueue == nullptr);
  DEBUG_ASSERT(resources.empty());
}

void D3D12GPU::initInfo() {
  _info.backend = Backend::D3D12;
  _info.version = "Direct3D 12";
  if (dxgiAdapter != nullptr) {
    DXGI_ADAPTER_DESC1 desc = {};
    dxgiAdapter->GetDesc1(&desc);
    std::wstring wRenderer(desc.Description);
    int sizeNeeded =
        WideCharToMultiByte(CP_UTF8, 0, wRenderer.data(), static_cast<int>(wRenderer.size()),
                            nullptr, 0, nullptr, nullptr);
    _info.renderer.resize(static_cast<size_t>(sizeNeeded));
    WideCharToMultiByte(CP_UTF8, 0, wRenderer.data(), static_cast<int>(wRenderer.size()),
                        _info.renderer.data(), sizeNeeded, nullptr, nullptr);
    if (desc.VendorId == 0x10DE) {
      _info.vendor = "NVIDIA";
    } else if (desc.VendorId == 0x1002) {
      _info.vendor = "AMD";
    } else if (desc.VendorId == 0x8086) {
      _info.vendor = "Intel";
    } else if (desc.VendorId == 0x1414) {
      _info.vendor = "Microsoft";
    } else {
      _info.vendor = "Unknown";
    }
  } else {
    _info.renderer = "Unknown D3D12 Device";
    _info.vendor = "Unknown";
  }
}

void D3D12GPU::initFeatures() {
  _features.semaphore = true;
  _features.clampToBorder = true;
  _features.textureBarrier = true;
}

void D3D12GPU::initLimits() {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
  if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options,
                                                 sizeof(options)))) {
    // D3D12 resource binding tier determines sampler limits.
    switch (options.ResourceBindingTier) {
      case D3D12_RESOURCE_BINDING_TIER_1:
        _limits.maxSamplersPerShaderStage = 16;
        break;
      case D3D12_RESOURCE_BINDING_TIER_2:
      case D3D12_RESOURCE_BINDING_TIER_3:
      default:
        _limits.maxSamplersPerShaderStage = 2048;
        break;
    }
  } else {
    _limits.maxSamplersPerShaderStage = 16;
  }
  _limits.maxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  _limits.maxUniformBufferBindingSize = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
  _limits.minUniformBufferOffsetAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
}

CommandQueue* D3D12GPU::queue() const {
  return commandQueue.get();
}

const shaderc::Compiler* D3D12GPU::shaderCompiler() const {
  return compiler.get();
}

bool D3D12GPU::isFormatRenderable(PixelFormat format) const {
  auto dxgiFormat = PixelFormatToDXGIFormat(format);
  if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
    return false;
  }
  D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = {};
  formatSupport.Format = static_cast<DXGI_FORMAT>(dxgiFormat);
  if (FAILED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport,
                                              sizeof(formatSupport)))) {
    return false;
  }
  return (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0;
}

std::shared_ptr<GPUBuffer> D3D12GPU::createBuffer(size_t size, uint32_t usage) {
  if (size == 0) {
    return nullptr;
  }
  return D3D12Buffer::Make(this, size, usage);
}

std::shared_ptr<Texture> D3D12GPU::createTexture(const TextureDescriptor& descriptor) {
  if (descriptor.width <= 0 || descriptor.height <= 0) {
    LOGE("D3D12GPU::createTexture() invalid dimensions: %dx%d", descriptor.width,
         descriptor.height);
    return nullptr;
  }
  if (!isFormatRenderable(descriptor.format) &&
      (descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    LOGE("D3D12GPU::createTexture() format not renderable for render attachment");
    return nullptr;
  }
  auto texture = D3D12Texture::Make(this, descriptor);
  if (texture == nullptr) {
    LOGE("D3D12GPU::createTexture() D3D12Texture::Make failed for %dx%d format=%d",
         descriptor.width, descriptor.height, static_cast<int>(descriptor.format));
  }
  return texture;
}

std::shared_ptr<Sampler> D3D12GPU::createSampler(const SamplerDescriptor& descriptor) {
  auto key = MakeSamplerKey(descriptor);
  auto iter = samplerCache.find(key);
  if (iter != samplerCache.end()) {
    return iter->second;
  }
  auto sampler = D3D12Sampler::Make(this, descriptor);
  if (sampler != nullptr) {
    samplerCache[key] = sampler;
  }
  return sampler;
}

uint32_t D3D12GPU::MakeSamplerKey(const SamplerDescriptor& descriptor) {
  uint32_t key = 0;
  key |= static_cast<uint32_t>(descriptor.addressModeX);
  key |= static_cast<uint32_t>(descriptor.addressModeY) << 3;
  key |= static_cast<uint32_t>(descriptor.minFilter) << 6;
  key |= static_cast<uint32_t>(descriptor.magFilter) << 8;
  key |= static_cast<uint32_t>(descriptor.mipmapMode) << 10;
  return key;
}

std::shared_ptr<ShaderModule> D3D12GPU::createShaderModule(const ShaderModuleDescriptor&) {
  // Will be implemented in sub-task 4 (D3D12ShaderModule).
  return nullptr;
}

std::shared_ptr<RenderPipeline> D3D12GPU::createRenderPipeline(const RenderPipelineDescriptor&) {
  // Will be implemented in sub-task 5 (D3D12RenderPipeline).
  return nullptr;
}

std::shared_ptr<CommandEncoder> D3D12GPU::createCommandEncoder() {
  processUnreferencedResources();
  // Will be implemented in sub-task 6 (D3D12CommandEncoder).
  return nullptr;
}

int D3D12GPU::getSampleCount(int requestedCount, PixelFormat pixelFormat) const {
  if (requestedCount <= 1) {
    return 1;
  }
  auto dxgiFormat = PixelFormatToDXGIFormat(pixelFormat);
  if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
    return 1;
  }
  for (int sampleCount = requestedCount; sampleCount <= D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
       sampleCount++) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels = {};
    qualityLevels.Format = static_cast<DXGI_FORMAT>(dxgiFormat);
    qualityLevels.SampleCount = static_cast<UINT>(sampleCount);
    if (SUCCEEDED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                                                   &qualityLevels, sizeof(qualityLevels))) &&
        qualityLevels.NumQualityLevels > 0) {
      return sampleCount;
    }
  }
  return 1;
}

std::vector<std::shared_ptr<Texture>> D3D12GPU::importHardwareTextures(HardwareBufferRef,
                                                                       uint32_t) {
  // D3D12 hardware buffer import is not supported yet.
  return {};
}

std::shared_ptr<Texture> D3D12GPU::importBackendTexture(const BackendTexture& backendTexture,
                                                        uint32_t usage, bool adopted) {
  if (backendTexture.backend() != Backend::D3D12) {
    return nullptr;
  }
  D3D12TextureInfo d3d12Info = {};
  if (!backendTexture.getD3D12TextureInfo(&d3d12Info) || d3d12Info.resource == nullptr) {
    return nullptr;
  }
  auto d3d12Resource =
      const_cast<ID3D12Resource*>(static_cast<const ID3D12Resource*>(d3d12Info.resource));
  ComPtr<ID3D12Resource> resource = nullptr;
  d3d12Resource->QueryInterface(IID_PPV_ARGS(&resource));
  if (resource == nullptr) {
    return nullptr;
  }
  return D3D12Texture::MakeFrom(this, std::move(resource), d3d12Info.format, usage, adopted);
}

std::shared_ptr<Texture> D3D12GPU::importBackendRenderTarget(
    const BackendRenderTarget& backendRenderTarget) {
  if (backendRenderTarget.backend() != Backend::D3D12) {
    return nullptr;
  }
  D3D12TextureInfo d3d12Info = {};
  if (!backendRenderTarget.getD3D12TextureInfo(&d3d12Info) || d3d12Info.resource == nullptr) {
    return nullptr;
  }
  auto format = backendRenderTarget.format();
  if (!isFormatRenderable(format)) {
    return nullptr;
  }
  auto d3d12Resource =
      const_cast<ID3D12Resource*>(static_cast<const ID3D12Resource*>(d3d12Info.resource));
  ComPtr<ID3D12Resource> resource = nullptr;
  d3d12Resource->QueryInterface(IID_PPV_ARGS(&resource));
  if (resource == nullptr) {
    return nullptr;
  }
  return D3D12Texture::MakeFrom(this, std::move(resource), d3d12Info.format,
                                TextureUsage::RENDER_ATTACHMENT, false);
}

std::shared_ptr<Semaphore> D3D12GPU::importBackendSemaphore(const BackendSemaphore& semaphore) {
  if (semaphore.backend() != Backend::D3D12) {
    return nullptr;
  }
  D3D12SyncInfo info = {};
  if (!semaphore.getD3D12Sync(&info) || info.fence == nullptr) {
    return nullptr;
  }
  auto rawFence = const_cast<ID3D12Fence*>(static_cast<const ID3D12Fence*>(info.fence));
  ComPtr<ID3D12Fence> fence = nullptr;
  rawFence->QueryInterface(IID_PPV_ARGS(&fence));
  if (fence == nullptr) {
    return nullptr;
  }
  return D3D12Semaphore::MakeFrom(this, std::move(fence), info.value);
}

BackendSemaphore D3D12GPU::stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr || semaphore.use_count() > 2) {
    return {};
  }
  return semaphore->getBackendSemaphore();
}

std::shared_ptr<D3D12Resource> D3D12GPU::addResource(D3D12Resource* resource) {
  DEBUG_ASSERT(resource != nullptr);
  resources.push_back(resource);
  resource->cachedPosition = --resources.end();
  return std::static_pointer_cast<D3D12Resource>(returnQueue->makeShared(resource));
}

void D3D12GPU::processUnreferencedResources() {
  DEBUG_ASSERT(returnQueue != nullptr);
  while (auto resource = static_cast<D3D12Resource*>(returnQueue->dequeue())) {
    resources.erase(resource->cachedPosition);
    resource->onRelease(this);
    delete resource;
  }
}

void D3D12GPU::releaseAll(bool releaseGPU) {
  samplerCache.clear();
  if (releaseGPU) {
    for (auto& resource : resources) {
      resource->onRelease(this);
    }
  }
  resources.clear();
  returnQueue = nullptr;
}

void D3D12GPU::reclaimAbandonedSession(D3D12FrameSession session) {
  // Letting the local go out of scope releases the command list and allocator via ComPtr, and
  // drops every shared_ptr in retainedResources. Resources whose refcount reaches zero enter the
  // ReturnQueue and will be destroyed by the next processUnreferencedResources() call.
  // Once InflightSubmission is introduced (later step), this method will be reused for the
  // post-fence reclaim path.
  (void)session;
}

}  // namespace tgfx
