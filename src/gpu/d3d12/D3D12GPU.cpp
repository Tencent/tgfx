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
#include <algorithm>
#include <string>
#include <vector>
#include "D3D12Buffer.h"
#include "D3D12CommandEncoder.h"
#include "D3D12CommandQueue.h"
#include "D3D12MipmapGenerator.h"
#include "D3D12RenderPipeline.h"
#include "D3D12Resource.h"
#include "D3D12Sampler.h"
#include "D3D12Semaphore.h"
#include "D3D12ShaderModule.h"
#include "D3D12Texture.h"
#include "core/utils/Log.h"

namespace tgfx {

bool HardwareBufferAvailable() {
  return false;
}

#ifdef TGFX_D3D12_DEBUG_LAYER
void D3D12GPU::drainDebugMessages(const char* tag) {
  if (d3d12Device == nullptr) {
    return;
  }
  ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
  if (FAILED(d3d12Device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
    return;
  }
  auto count = infoQueue->GetNumStoredMessages();
  for (UINT64 i = 0; i < count; i++) {
    SIZE_T msgLen = 0;
    infoQueue->GetMessage(i, nullptr, &msgLen);
    std::vector<char> buf(msgLen);
    auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
    if (SUCCEEDED(infoQueue->GetMessage(i, msg, &msgLen))) {
      LOGE("[D3D12 debug @ %s] %.*s", tag, static_cast<int>(msg->DescriptionByteLength),
           msg->pDescription);
    }
  }
  if (count > 0) {
    infoQueue->ClearStoredMessages();
  }
}
#else
void D3D12GPU::drainDebugMessages(const char*) {
}
#endif

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
  if (gpu->commandQueue == nullptr || gpu->_frameFence == nullptr ||
      gpu->_frameFenceEvent == nullptr || gpu->_srvRing.heap() == nullptr ||
      gpu->_samplerHeap == nullptr) {
    return nullptr;
  }
  return gpu;
}

D3D12GPU::D3D12GPU(ComPtr<ID3D12Device> device, ComPtr<IDXGIAdapter1> adapter)
    : d3d12Device(std::move(device)), dxgiAdapter(std::move(adapter)) {
  initInfo();
  initFeatures();
  initLimits();
  // Create the per-frame fence and its waitable event before the command queue, since the queue
  // uses these handles when scheduling Signal/Wait operations.
  if (FAILED(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_frameFence)))) {
    LOGE("D3D12GPU: failed to create frame fence.");
    return;
  }
  _frameFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (_frameFenceEvent == nullptr) {
    LOGE("D3D12GPU: failed to create frame fence event.");
    _frameFence = nullptr;
    return;
  }
  // Allocate the process-wide shader-visible CBV/SRV/UAV ring up front. Failure here means we
  // cannot satisfy any subsequent render-pass binding, so propagate it through to Make() via the
  // null-heap check rather than letting the GPU come up half-initialised.
  if (!_srvRing.init(d3d12Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                     SRV_RING_CAPACITY)) {
    LOGE("D3D12GPU: failed to initialise CBV/SRV/UAV descriptor ring.");
    return;
  }
  // Allocate the process-wide shader-visible Sampler heap. Append-only, capped at D3D12's hard
  // 2048-descriptor limit. Each unique SamplerDescriptor consumes one slot for the lifetime of
  // the GPU instance.
  D3D12_DESCRIPTOR_HEAP_DESC samplerDesc = {};
  samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  samplerDesc.NumDescriptors = SAMPLER_HEAP_CAPACITY;
  samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(d3d12Device->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(&_samplerHeap)))) {
    LOGE("D3D12GPU: failed to create shader-visible Sampler heap.");
    return;
  }
  _samplerHeapCapacity = SAMPLER_HEAP_CAPACITY;
  _samplerDescriptorIncrement =
      d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  commandQueue = std::make_unique<D3D12CommandQueue>(this);
  compiler = std::make_unique<shaderc::Compiler>();
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12GPU::allocatePermanentSamplerSlot(
    const D3D12_SAMPLER_DESC& desc) {
  D3D12_GPU_DESCRIPTOR_HANDLE invalid = {};
  if (_samplerHeap == nullptr || _samplerHeapSize >= _samplerHeapCapacity) {
    LOGE("D3D12GPU::allocatePermanentSamplerSlot: sampler heap exhausted (%u/%u).",
         _samplerHeapSize, _samplerHeapCapacity);
    return invalid;
  }
  auto slot = _samplerHeapSize++;
  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = _samplerHeap->GetCPUDescriptorHandleForHeapStart();
  cpuHandle.ptr += static_cast<SIZE_T>(slot) * _samplerDescriptorIncrement;
  d3d12Device->CreateSampler(&desc, cpuHandle);
  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = _samplerHeap->GetGPUDescriptorHandleForHeapStart();
  gpuHandle.ptr += static_cast<UINT64>(slot) * _samplerDescriptorIncrement;
  return gpuHandle;
}

D3D12GPU::~D3D12GPU() {
  DEBUG_ASSERT(returnQueue == nullptr);
  DEBUG_ASSERT(resources.empty());
  if (_frameFenceEvent != nullptr) {
    CloseHandle(_frameFenceEvent);
    _frameFenceEvent = nullptr;
  }
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
  // Vulkan/Metal back-ends report any colour or depth-stencil attachment format as renderable.
  // Match that language so callers using a backend-agnostic gate (e.g. createTexture's
  // RENDER_ATTACHMENT pre-check) work uniformly when the format happens to be depth-stencil.
  constexpr UINT attachmentMask =
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
  return (formatSupport.Support1 & attachmentMask) != 0;
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

std::shared_ptr<ShaderModule> D3D12GPU::createShaderModule(
    const ShaderModuleDescriptor& descriptor) {
  return D3D12ShaderModule::Make(this, descriptor);
}

std::shared_ptr<RenderPipeline> D3D12GPU::createRenderPipeline(
    const RenderPipelineDescriptor& descriptor) {
  return D3D12RenderPipeline::Make(this, descriptor);
}

std::shared_ptr<CommandEncoder> D3D12GPU::createCommandEncoder() {
  processUnreferencedResources();
  return D3D12CommandEncoder::Make(this);
}

D3D12MipmapGenerator* D3D12GPU::mipmapGenerator() {
  if (_mipmapGenerator == nullptr) {
    _mipmapGenerator = std::unique_ptr<D3D12MipmapGenerator>(new D3D12MipmapGenerator(this));
    if (!_mipmapGenerator->isReady()) {
      _mipmapGenerator = nullptr;
    }
  }
  return _mipmapGenerator.get();
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

// Map a D3D12_AUTO_BREADCRUMB_OP enum value to a short readable string. Not exhaustive — only the
// ops the TGFX backend actually emits are listed; everything else falls through to "<other>".
static const char* AutoBreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op) {
  switch (op) {
    case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:
      return "SetMarker";
    case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:
      return "BeginEvent";
    case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:
      return "EndEvent";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:
      return "DrawInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:
      return "DrawIndexedInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:
      return "ExecuteIndirect";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:
      return "Dispatch";
    case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:
      return "CopyBufferRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:
      return "CopyTextureRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:
      return "CopyResource";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:
      return "ResolveSubresource";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:
      return "ClearRenderTargetView";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:
      return "ClearDepthStencilView";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:
      return "ClearUAV";
    case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:
      return "ResourceBarrier";
    case D3D12_AUTO_BREADCRUMB_OP_PRESENT:
      return "Present";
    default:
      return "<other>";
  }
}

void D3D12GPU::markContextLost(const char* tag) {
  if (contextLost) {
    return;
  }
  contextLost = true;
  dumpDeviceRemovedExtendedData(tag);
}

void D3D12GPU::dumpDeviceRemovedExtendedData(const char* tag) {
  if (d3d12Device == nullptr) {
    return;
  }
  auto reason = d3d12Device->GetDeviceRemovedReason();
  if (SUCCEEDED(reason)) {
    return;
  }
  // Some drivers populate DRED breadcrumb buffers asynchronously after the device transitions to
  // a removed state. Sleep briefly so the breadcrumb / page-fault output is fully formed before
  // we query it. Diagnostic-only path; cost is bounded to a couple of milliseconds at fault time.
  Sleep(50);
  LOGE("[DRED %s] device removed, reason=0x%08X", tag, static_cast<unsigned>(reason));

  ComPtr<ID3D12DeviceRemovedExtendedData> dred = nullptr;
  if (FAILED(d3d12Device.As(&dred))) {
    LOGE("[DRED %s] DRED interface unavailable on this device.", tag);
    return;
  }

  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbsOutput = {};
  if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbsOutput))) {
    auto* node = breadcrumbsOutput.pHeadAutoBreadcrumbNode;
    if (node == nullptr) {
      LOGE("[DRED %s] auto-breadcrumb list is empty — either no command list executed before "
           "the fault, or the driver did not record breadcrumbs (verify "
           "SetAutoBreadcrumbsEnablement(FORCED_ON) was called before D3D12CreateDevice).",
           tag);
    }
    int nodeIndex = 0;
    while (node != nullptr) {
      const char* listName =
          node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "<unnamed>";
      const char* queueName =
          node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "<unnamed>";
      auto last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0u;
      auto count = node->BreadcrumbCount;
      LOGE("[DRED %s] node %d: list='%s' queue='%s' completed=%u/%u", tag, nodeIndex, listName,
           queueName, last, count);
      // Print a small window around the last completed op so we can see the failing call.
      uint32_t windowStart = last >= 4 ? last - 4 : 0;
      uint32_t windowEnd = (last + 8 < count) ? (last + 8) : count;
      for (uint32_t i = windowStart; i < windowEnd; i++) {
        const char* marker = (i == last) ? "  >>>" : "     ";
        LOGE("[DRED %s] %s op[%u] = %s", tag, marker, i,
             AutoBreadcrumbOpName(node->pCommandHistory[i]));
      }
      node = node->pNext;
      nodeIndex++;
    }
  } else {
    LOGE("[DRED %s] GetAutoBreadcrumbsOutput failed.", tag);
  }

  D3D12_DRED_PAGE_FAULT_OUTPUT pageFaultOutput = {};
  if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFaultOutput))) {
    if (pageFaultOutput.PageFaultVA != 0) {
      LOGE("[DRED %s] page fault VA = 0x%llx", tag,
           static_cast<unsigned long long>(pageFaultOutput.PageFaultVA));
      auto* existing = pageFaultOutput.pHeadExistingAllocationNode;
      while (existing != nullptr) {
        LOGE("[DRED %s] existing alloc near fault: '%s' type=%d", tag,
             existing->ObjectNameA ? existing->ObjectNameA : "<unnamed>",
             static_cast<int>(existing->AllocationType));
        existing = existing->pNext;
      }
      auto* recent = pageFaultOutput.pHeadRecentFreedAllocationNode;
      while (recent != nullptr) {
        LOGE("[DRED %s] recently freed: '%s' type=%d", tag,
             recent->ObjectNameA ? recent->ObjectNameA : "<unnamed>",
             static_cast<int>(recent->AllocationType));
        recent = recent->pNext;
      }
    }
  }
}

void D3D12GPU::releaseAll(bool releaseGPU) {
  // Shutdown ordering must wait for all GPU work to complete before destroying anything that the
  // GPU may still be reading. The Vulkan backend does the same via waitAllInflightSubmissions().
  if (releaseGPU) {
    waitAllInflightSubmissions();
  } else {
    // Even when releaseGPU is false (test teardown), inflight sessions must drop their references
    // so we don't leak the underlying D3D12 resources captured by retainedResources.
    inflightSubmissions.clear();
  }
  samplerCache.clear();
  // Drop pooled command allocator/list pairs. Done after wait so the GPU is no longer using
  // them; doing it before would still be safe (ComPtrs hold the only references) but ordering
  // matches the rest of the shutdown path.
  _commandListPool.clear();
  // Drop the cached mipmap generator's root signature + PSO before tearing the device down.
  // Resources retained by inflight submissions have already been released above.
  _mipmapGenerator = nullptr;
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
  // drops every shared_ptr in retainedResources/retainedDescriptorHeaps/retainedRTVDSVHeaps.
  // Resources whose refcount reaches zero enter the ReturnQueue and will be destroyed by the
  // next processUnreferencedResources() call.
  (void)session;
}

void D3D12GPU::executeSubmission(SubmitRequest request) {
  // If the GPU has already reported a fatal error, drop the submission without waiting on the
  // fence — DXGI_ERROR_DEVICE_REMOVED is sticky and the fence will never advance. Local cleanup
  // still runs because session/uploads destructors release their D3D12 references.
  if (contextLost) {
    reclaimAbandonedSession(std::move(request.session));
    request.uploads.clear();
    return;
  }

  // Step 1: Non-blocking reclaim of any submissions whose fence values have already signalled.
  pollCompletedSubmissions();

  // Step 2: Backpressure — block until the oldest inflight submission completes if we have
  // already filled the in-flight pipeline. A bounded timeout protects against TDR scenarios
  // where the GPU never advances; on timeout we mark the context lost and stop tracking it.
  if (inflightSubmissions.size() >= MAX_FRAMES_IN_FLIGHT) {
    auto& oldest = inflightSubmissions.front();
    if (_frameFence->GetCompletedValue() < oldest.fenceValue) {
      _frameFence->SetEventOnCompletion(oldest.fenceValue, _frameFenceEvent);
      auto waitResult = WaitForSingleObject(_frameFenceEvent, 5000);
      if (waitResult != WAIT_OBJECT_0) {
        LOGE("D3D12GPU::executeSubmission: backpressure wait timed out (target=%llu), "
             "marking context lost.",
             static_cast<unsigned long long>(oldest.fenceValue));
        markContextLost("executeSubmission backpressure timeout");
        reclaimAbandonedSession(std::move(request.session));
        request.uploads.clear();
        while (!inflightSubmissions.empty()) {
          reclaimSubmission(inflightSubmissions.front());
          inflightSubmissions.pop_front();
        }
        return;
      }
    }
    pollCompletedSubmissions();
    // Re-check device removal after the wait — if the GPU TDR'd while we were blocked, the
    // event would have been signalled but no work has actually completed. Detect this and exit
    // the inflight queue cleanup path immediately.
    if (FAILED(d3d12Device->GetDeviceRemovedReason())) {
      markContextLost("executeSubmission backpressure post-check");
      reclaimAbandonedSession(std::move(request.session));
      request.uploads.clear();
      // Drop every still-tracked submission since the GPU will never signal again.
      while (!inflightSubmissions.empty()) {
        reclaimSubmission(inflightSubmissions.front());
        inflightSubmissions.pop_front();
      }
      return;
    }
  }

  auto cmdQueue = commandQueue->d3d12CommandQueue();
  if (cmdQueue == nullptr) {
    LOGE("D3D12GPU::executeSubmission: command queue is null, abandoning session.");
    reclaimAbandonedSession(std::move(request.session));
    return;
  }

  // Step 3: Optional cross-queue wait. D3D12Semaphore stores its target value as a host-readable
  // member; the GPU side simply re-uses ID3D12CommandQueue::Wait().
  if (request.waitSemaphore != nullptr) {
    auto fence = request.waitSemaphore->d3d12Fence();
    if (fence != nullptr) {
      cmdQueue->Wait(fence, request.waitSemaphore->signalValue());
    }
  }

  // Step 4: Execute auxiliary command lists (e.g. texture upload lists recorded by the queue
  // outside the main render command list) followed by the main render command list. Order
  // matters: uploads must complete before the render list can sample the destination textures.
  std::vector<ID3D12CommandList*> lists;
  lists.reserve(request.session.auxCommandLists.size() + 1);
  for (auto& aux : request.session.auxCommandLists) {
    if (aux != nullptr) {
      lists.push_back(aux.Get());
    }
  }
  if (request.session.commandList != nullptr) {
    lists.push_back(request.session.commandList.Get());
  }
  if (!lists.empty()) {
    cmdQueue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
  }
#ifdef TGFX_D3D12_DEBUG_LAYER
  drainDebugMessages("executeSubmission");
#endif

  // Step 5: Optional signal of an external semaphore. The semaphore exposes a fixed fence value
  // assigned at creation time — we just signal that value on this queue. After signalling, bump
  // the semaphore's internal value so a subsequent insertSemaphore() call would see a fresh
  // generation if the same fence object is re-used.
  if (request.signalSemaphore != nullptr) {
    auto fence = request.signalSemaphore->d3d12Fence();
    if (fence != nullptr) {
      auto target = request.signalSemaphore->nextSignalValue();
      cmdQueue->Signal(fence, target);
      request.signalSemaphore->commitSignalValue();
    }
    // Keep the semaphore alive until the GPU has consumed the Signal command. Without this
    // retention the application could drop its last reference and the underlying ID3D12Fence
    // would be released before the GPU is done with it.
    request.session.retainedResources.push_back(std::move(request.signalSemaphore));
  }
  if (request.waitSemaphore != nullptr) {
    request.session.retainedResources.push_back(std::move(request.waitSemaphore));
  }

  // Step 6: Signal the GPU's internal frame fence so we can later detect completion of this
  // submission and reclaim its session. If the Signal call itself fails (it can return
  // DXGI_ERROR_DEVICE_REMOVED if the GPU TDR'd while the previous ExecuteCommandLists was
  // executing), trip the contextLost flag so subsequent calls don't block on a fence that will
  // never advance.
  ++_lastSignalledFenceValue;
  // Tag every CBV/SRV/UAV descriptor-ring slot allocated during this submission with the
  // about-to-be-signalled fence value. Once that fence value completes, the slots become
  // reclaimable in retire(); see pollCompletedSubmissions(). The Sampler heap is append-only
  // (slots persist for the GPU's lifetime) and does not need fence tracking.
  _srvRing.commit(_lastSignalledFenceValue);
  auto signalHr = cmdQueue->Signal(_frameFence.Get(), _lastSignalledFenceValue);
  if (FAILED(signalHr) || FAILED(d3d12Device->GetDeviceRemovedReason())) {
    LOGE("D3D12GPU::executeSubmission: Signal failed (HRESULT=0x%08X) or device removed; "
         "marking context lost.",
         static_cast<unsigned>(signalHr));
    markContextLost("executeSubmission Signal");
    reclaimAbandonedSession(std::move(request.session));
    request.uploads.clear();
    while (!inflightSubmissions.empty()) {
      reclaimSubmission(inflightSubmissions.front());
      inflightSubmissions.pop_front();
    }
    return;
  }

  InflightSubmission inflight = {};
  inflight.fenceValue = _lastSignalledFenceValue;
  inflight.session = std::move(request.session);
  inflight.uploads = std::move(request.uploads);
  inflightSubmissions.push_back(std::move(inflight));
}

void D3D12GPU::waitAllInflightSubmissions() {
  if (_frameFence == nullptr) {
    return;
  }
  if (contextLost || FAILED(d3d12Device->GetDeviceRemovedReason())) {
    // Device is gone — fence will never advance again. Drop everything synchronously.
    markContextLost("waitAllInflightSubmissions entry");
    while (!inflightSubmissions.empty()) {
      reclaimSubmission(inflightSubmissions.front());
      inflightSubmissions.pop_front();
    }
    processUnreferencedResources();
    return;
  }
  if (!inflightSubmissions.empty()) {
    auto& last = inflightSubmissions.back();
    if (_frameFence->GetCompletedValue() < last.fenceValue) {
      // Use a finite timeout instead of INFINITE so we never hang the application even if some
      // earlier submission had a corrupted command list that prevents the GPU from advancing.
      // 5 seconds is well past any sensible draw frame budget; if it expires we fall through to
      // the device-removal check below.
      _frameFence->SetEventOnCompletion(last.fenceValue, _frameFenceEvent);
      auto waitResult = WaitForSingleObject(_frameFenceEvent, 5000);
      if (waitResult != WAIT_OBJECT_0) {
        LOGE("D3D12GPU::waitAllInflightSubmissions: fence wait timed out (target=%llu), "
             "marking context lost.",
             static_cast<unsigned long long>(last.fenceValue));
        markContextLost("waitAllInflightSubmissions timeout");
        while (!inflightSubmissions.empty()) {
          reclaimSubmission(inflightSubmissions.front());
          inflightSubmissions.pop_front();
        }
        processUnreferencedResources();
        return;
      }
    }
  }
  pollCompletedSubmissions();
}

uint64_t D3D12GPU::completedFenceValue() const {
  return _frameFence != nullptr ? _frameFence->GetCompletedValue() : 0;
}

void D3D12GPU::pollCompletedSubmissions() {
  if (_frameFence == nullptr) {
    return;
  }
  auto completed = _frameFence->GetCompletedValue();
  while (!inflightSubmissions.empty() && inflightSubmissions.front().fenceValue <= completed) {
    reclaimSubmission(inflightSubmissions.front());
    inflightSubmissions.pop_front();
  }
  // Free shader-visible CBV/SRV/UAV descriptor slots whose owning submissions have signalled.
  // Sampler slots persist for the GPU's lifetime so they need no per-fence retirement.
  _srvRing.retire(completed);
  // Releasing retained shared_ptrs may have moved D3D12Resource instances into the return queue;
  // free them now so the caller sees up-to-date memory accounting.
  processUnreferencedResources();
}

void D3D12GPU::reclaimSubmission(InflightSubmission& submission) {
  // The GPU has signalled the fence value associated with this submission, so every command
  // allocator/list pair it referenced is safe to reuse. Return them to the pool before tearing
  // down the rest of the session — once the FrameSession destructor runs, the ComPtrs are gone.
  if (submission.session.commandAllocator != nullptr &&
      submission.session.commandList != nullptr) {
    D3D12CommandListPool::Entry entry = {};
    entry.allocator = std::move(submission.session.commandAllocator);
    entry.commandList = std::move(submission.session.commandList);
    _commandListPool.release(std::move(entry));
  }
  // Auxiliary command lists (e.g. transient upload lists recorded by D3D12CommandQueue::submit)
  // are paired one-to-one with their auxAllocators by index; recycle them together so the pool
  // sees consistent (allocator, list) pairs.
  size_t auxCount = std::min(submission.session.auxAllocators.size(),
                             submission.session.auxCommandLists.size());
  for (size_t i = 0; i < auxCount; i++) {
    if (submission.session.auxAllocators[i] != nullptr &&
        submission.session.auxCommandLists[i] != nullptr) {
      D3D12CommandListPool::Entry entry = {};
      entry.allocator = std::move(submission.session.auxAllocators[i]);
      entry.commandList = std::move(submission.session.auxCommandLists[i]);
      _commandListPool.release(std::move(entry));
    }
  }
  // The remaining ComPtr / shared_ptr destructors in FrameSession handle descriptor heaps,
  // retained resources, and staging UPLOAD buffers.
  submission.session = D3D12FrameSession{};
  submission.uploads.clear();
}

}  // namespace tgfx
