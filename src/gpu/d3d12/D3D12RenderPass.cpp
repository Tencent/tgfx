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

#include "D3D12RenderPass.h"
#include "D3D12Buffer.h"
#include "D3D12CommandEncoder.h"
#include "D3D12GPU.h"
#include "D3D12RenderPipeline.h"
#include "D3D12Sampler.h"
#include "D3D12Texture.h"
#include "core/utils/Log.h"

namespace tgfx {

// Capacity of the per-render-pass shader-visible descriptor heaps.
//
// CBV/SRV/UAV heap: setTexture() allocates one SRV slot per unique (resource, format, mipLevels)
// triple so its capacity bounds the number of *distinct* textures sampled in this pass, not the
// total number of setTexture() calls.
//
// Sampler heap: D3D12 caps a single shader-visible sampler heap at 2048 descriptors. Because
// every typical TGFX render pass binds far fewer than that — sampler descriptors are heavily
// reused across draws — a small cap of 256 unique samplers is plenty and keeps the per-pass
// memory footprint low. setTexture() also dedups by D3D12Sampler pointer (samplers are cached
// at the GPU level by SamplerDescriptor), so repeated bindings of the same sampler don't
// consume new slots.
static constexpr uint32_t kRenderPassSrvHeapSize = 1024;
static constexpr uint32_t kRenderPassSamplerHeapSize = 256;

static D3D12_PRIMITIVE_TOPOLOGY ToD3D12Topology(PrimitiveType type) {
  return type == PrimitiveType::TriangleStrip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
                                              : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

std::shared_ptr<D3D12RenderPass> D3D12RenderPass::Make(D3D12CommandEncoder* encoder,
                                                       const RenderPassDescriptor& descriptor) {
  if (encoder == nullptr) {
    return nullptr;
  }
  auto gpu = static_cast<D3D12GPU*>(encoder->gpu());
  auto pass = std::shared_ptr<D3D12RenderPass>(new D3D12RenderPass(encoder, gpu, descriptor));
  if (!pass->initialise(descriptor)) {
    return nullptr;
  }
  return pass;
}

D3D12RenderPass::D3D12RenderPass(D3D12CommandEncoder* encoder, D3D12GPU* gpu,
                                 const RenderPassDescriptor& passDescriptor)
    : RenderPass(passDescriptor), encoder(encoder), d3d12GPU(gpu),
      commandList(encoder->d3d12CommandList()) {
}

bool D3D12RenderPass::initialise(const RenderPassDescriptor& passDescriptor) {
  if (commandList == nullptr) {
    return false;
  }
  auto device = d3d12GPU->device();

  // Step 1: Allocate and populate the per-pass RTV heap.
  uint32_t numColorAttachments = 0;
  for (auto& ca : passDescriptor.colorAttachments) {
    if (ca.texture != nullptr) {
      numColorAttachments++;
    }
  }
  bool hasDepth = (passDescriptor.depthStencilAttachment.texture != nullptr);

  ComPtr<ID3D12DescriptorHeap> rtvHeap = nullptr;
  if (numColorAttachments > 0) {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = numColorAttachments;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)))) {
      LOGE("D3D12RenderPass: failed to create RTV heap.");
      return false;
    }
  }

  ComPtr<ID3D12DescriptorHeap> dsvHeap = nullptr;
  if (hasDepth) {
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)))) {
      LOGE("D3D12RenderPass: failed to create DSV heap.");
      return false;
    }
  }

  auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
  rtvHandles.reserve(numColorAttachments);
  uint32_t fbWidth = 0;
  uint32_t fbHeight = 0;

  for (auto& ca : passDescriptor.colorAttachments) {
    if (ca.texture == nullptr) {
      continue;
    }
    auto d3d12Tex = std::static_pointer_cast<D3D12Texture>(ca.texture);
    encoder->retainResource(d3d12Tex);
    colorAttachments.push_back(d3d12Tex);

    // Transition to RENDER_TARGET state before clearing or rendering. The "current" state is
    // either COMMON (newly created or coming back from sampling) or COLOR_ATTACHMENT from a
    // preceding render pass that already left it there. Both transitions are valid.
    TransitionResourceState(commandList, d3d12Tex->d3d12Resource(), d3d12Tex->currentState(),
                            D3D12_RESOURCE_STATE_RENDER_TARGET);
    d3d12Tex->setCurrentState(D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(rtvHandles.size()) * rtvDescriptorSize;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = static_cast<DXGI_FORMAT>(d3d12Tex->dxgiFormat());
    rtvDesc.ViewDimension = (d3d12Tex->sampleCount() > 1) ? D3D12_RTV_DIMENSION_TEXTURE2DMS
                                                          : D3D12_RTV_DIMENSION_TEXTURE2D;
    if (rtvDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D) {
      rtvDesc.Texture2D.MipSlice = 0;
      rtvDesc.Texture2D.PlaneSlice = 0;
    }
    device->CreateRenderTargetView(d3d12Tex->d3d12Resource(), &rtvDesc, rtvHandle);
    rtvHandles.push_back(rtvHandle);

    if (ca.loadAction == LoadAction::Clear) {
      const float clear[4] = {ca.clearValue.red, ca.clearValue.green, ca.clearValue.blue,
                              ca.clearValue.alpha};
      commandList->ClearRenderTargetView(rtvHandle, clear, 0, nullptr);
    }

    fbWidth = static_cast<uint32_t>(d3d12Tex->width());
    fbHeight = static_cast<uint32_t>(d3d12Tex->height());

    // Capture the optional resolve target for this attachment. A null entry keeps the parallel
    // vector aligned with colorAttachments so the onEnd() loop can match them by index.
    if (ca.resolveTexture != nullptr) {
      auto resolveTex = std::static_pointer_cast<D3D12Texture>(ca.resolveTexture);
      encoder->retainResource(resolveTex);
      resolveTextures.push_back(std::move(resolveTex));
    } else {
      resolveTextures.push_back(nullptr);
    }
  }

  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
  if (hasDepth) {
    auto d3d12Tex =
        std::static_pointer_cast<D3D12Texture>(passDescriptor.depthStencilAttachment.texture);
    encoder->retainResource(d3d12Tex);
    depthStencilAttachment = d3d12Tex;

    TransitionResourceState(commandList, d3d12Tex->d3d12Resource(), d3d12Tex->currentState(),
                            D3D12_RESOURCE_STATE_DEPTH_WRITE);
    d3d12Tex->setCurrentState(D3D12_RESOURCE_STATE_DEPTH_WRITE);

    dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = static_cast<DXGI_FORMAT>(d3d12Tex->dxgiFormat());
    dsvDesc.ViewDimension = (d3d12Tex->sampleCount() > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMS
                                                          : D3D12_DSV_DIMENSION_TEXTURE2D;
    if (dsvDesc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D) {
      dsvDesc.Texture2D.MipSlice = 0;
    }
    device->CreateDepthStencilView(d3d12Tex->d3d12Resource(), &dsvDesc, dsvHandle);

    if (passDescriptor.depthStencilAttachment.loadAction == LoadAction::Clear) {
      D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
      commandList->ClearDepthStencilView(
          dsvHandle, clearFlags, passDescriptor.depthStencilAttachment.depthClearValue,
          static_cast<UINT8>(passDescriptor.depthStencilAttachment.stencilClearValue), 0, nullptr);
    }

    if (fbWidth == 0) {
      fbWidth = static_cast<uint32_t>(d3d12Tex->width());
      fbHeight = static_cast<uint32_t>(d3d12Tex->height());
    }
  }

  // Step 2: Bind render targets.
  commandList->OMSetRenderTargets(static_cast<UINT>(rtvHandles.size()),
                                  rtvHandles.empty() ? nullptr : rtvHandles.data(), FALSE,
                                  hasDepth ? &dsvHandle : nullptr);

  // Step 3: Default viewport / scissor covering the entire framebuffer.
  D3D12_VIEWPORT viewport = {};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(fbWidth);
  viewport.Height = static_cast<float>(fbHeight);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  commandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissor = {};
  scissor.left = 0;
  scissor.top = 0;
  scissor.right = static_cast<LONG>(fbWidth);
  scissor.bottom = static_cast<LONG>(fbHeight);
  commandList->RSSetScissorRects(1, &scissor);

  // Step 4: Allocate shader-visible descriptor heaps for this pass and let the encoder hold them.
  if (!allocateShaderVisibleHeaps()) {
    return false;
  }
  if (rtvHeap != nullptr) {
    encoder->retainRTVDSVHeap(std::move(rtvHeap));
  }
  if (dsvHeap != nullptr) {
    encoder->retainRTVDSVHeap(std::move(dsvHeap));
  }
  return true;
}

bool D3D12RenderPass::allocateShaderVisibleHeaps() {
  auto device = d3d12GPU->device();

  D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
  srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srvHeapDesc.NumDescriptors = kRenderPassSrvHeapSize;
  srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)))) {
    LOGE("D3D12RenderPass: failed to create shader-visible CBV/SRV/UAV heap.");
    d3d12GPU->drainDebugMessages("D3D12RenderPass::allocateShaderVisibleHeaps");
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
  samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  samplerHeapDesc.NumDescriptors = kRenderPassSamplerHeapSize;
  samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&samplerHeap)))) {
    LOGE("D3D12RenderPass: failed to create shader-visible Sampler heap.");
    srvHeap = nullptr;
    return false;
  }

  srvDescriptorSize =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  samplerDescriptorSize =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  srvHeapCapacity = kRenderPassSrvHeapSize;
  samplerHeapCapacity = kRenderPassSamplerHeapSize;

  encoder->retainDescriptorHeap(srvHeap);
  encoder->retainDescriptorHeap(samplerHeap);

  // Bind both heaps once. D3D12 allows at most one CBV/SRV/UAV heap and one Sampler heap to be
  // active simultaneously, so binding here covers every draw within this render pass.
  ID3D12DescriptorHeap* heaps[] = {srvHeap.Get(), samplerHeap.Get()};
  commandList->SetDescriptorHeaps(2, heaps);
  descriptorHeapsBoundToCmdList = true;
  return true;
}

GPU* D3D12RenderPass::gpu() const {
  return d3d12GPU;
}

void D3D12RenderPass::setViewport(int x, int y, int width, int height) {
  D3D12_VIEWPORT viewport = {};
  viewport.TopLeftX = static_cast<float>(x);
  viewport.TopLeftY = static_cast<float>(y);
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  commandList->RSSetViewports(1, &viewport);
}

void D3D12RenderPass::setScissorRect(int x, int y, int width, int height) {
  D3D12_RECT scissor = {};
  scissor.left = x;
  scissor.top = y;
  scissor.right = x + width;
  scissor.bottom = y + height;
  commandList->RSSetScissorRects(1, &scissor);
}

void D3D12RenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (!pipeline) {
    return;
  }
  auto d3d12Pipeline = std::static_pointer_cast<D3D12RenderPipeline>(pipeline);
  if (currentPipeline == d3d12Pipeline) {
    return;
  }
  if (d3d12Pipeline->d3d12PipelineState() == nullptr ||
      d3d12Pipeline->d3d12RootSignature() == nullptr) {
    return;
  }
  currentPipeline = d3d12Pipeline;
  encoder->retainResource(d3d12Pipeline);
  commandList->SetPipelineState(d3d12Pipeline->d3d12PipelineState());
  commandList->SetGraphicsRootSignature(d3d12Pipeline->d3d12RootSignature());

  // Switching pipelines invalidates root parameter state, so re-flag every binding as dirty.
  for (auto& ub : uniformBindings) {
    if (ub.gpuAddress != 0) {
      ub.dirty = true;
    }
  }
  for (auto& tb : textureBindings) {
    if (tb.srvTableStart.ptr != 0) {
      tb.dirty = true;
    }
  }
}

void D3D12RenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset, size_t /*size*/) {
  if (!buffer || binding >= MaxUniformBindings) {
    return;
  }
  auto d3d12Buffer = std::static_pointer_cast<D3D12Buffer>(buffer);
  encoder->retainResource(d3d12Buffer);
  auto gpuAddr = d3d12Buffer->d3d12Resource()->GetGPUVirtualAddress() + offset;
  auto& ub = uniformBindings[binding];
  if (ub.gpuAddress != gpuAddr) {
    ub.gpuAddress = gpuAddr;
    ub.dirty = true;
  }
}

void D3D12RenderPass::setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                                 std::shared_ptr<Sampler> sampler) {
  if (!texture || !sampler || !currentPipeline || binding >= MaxTextureBindings) {
    return;
  }
  auto d3d12Tex = std::static_pointer_cast<D3D12Texture>(texture);
  auto d3d12Samp = std::static_pointer_cast<D3D12Sampler>(sampler);
  encoder->retainResource(d3d12Tex);
  encoder->retainResource(d3d12Samp);

  // Color render targets and write-back textures may currently be in RENDER_TARGET / COPY_DEST.
  // Transition to PIXEL_SHADER_RESOURCE so the SRV is valid. The Vulkan backend keeps everything
  // in GENERAL layout; D3D12 has no equivalent and must transition explicitly.
  auto current = d3d12Tex->currentState();
  if (current != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
    TransitionResourceState(commandList, d3d12Tex->d3d12Resource(), current,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    d3d12Tex->setCurrentState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // Track this texture so onEnd() can transition it back to COMMON. Without that step, D3D12
    // automatic state decay after ExecuteCommandLists drops the resource to COMMON, but our CPU
    // tracker still believes it is in PIXEL_SHADER_RESOURCE — every subsequent transition then
    // fails "Before state mismatch" validation and (on some drivers) destabilises the device.
    shaderResourceTextures.push_back(d3d12Tex);
  }

  auto device = d3d12GPU->device();

  // SRV slot: dedup by (resource, format, mipLevels). Repeated bindings of the same texture
  // within one render pass share a single descriptor.
  SrvCacheKey srvKey = {};
  srvKey.resource = d3d12Tex->d3d12Resource();
  srvKey.format = static_cast<DXGI_FORMAT>(d3d12Tex->dxgiFormat());
  srvKey.mipLevels = static_cast<UINT>(d3d12Tex->mipLevelCount());
  D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = {};
  auto srvIt = srvSlotCache.find(srvKey);
  if (srvIt != srvSlotCache.end()) {
    srvGpu = srvIt->second;
  } else {
    if (srvHeapHead >= srvHeapCapacity) {
      LOGE("D3D12RenderPass::setTexture: SRV heap exhausted (capacity=%u).", srvHeapCapacity);
      return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr += static_cast<SIZE_T>(srvHeapHead) * srvDescriptorSize;
    srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
    srvGpu.ptr += static_cast<UINT64>(srvHeapHead) * srvDescriptorSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = srvKey.format;
    srvDesc.ViewDimension = (d3d12Tex->sampleCount() > 1) ? D3D12_SRV_DIMENSION_TEXTURE2DMS
                                                          : D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (srvDesc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D) {
      srvDesc.Texture2D.MostDetailedMip = 0;
      srvDesc.Texture2D.MipLevels = srvKey.mipLevels;
      srvDesc.Texture2D.PlaneSlice = 0;
      srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    }
    device->CreateShaderResourceView(d3d12Tex->d3d12Resource(), &srvDesc, srvCpu);
    srvHeapHead++;
    srvSlotCache.emplace(srvKey, srvGpu);
  }

  // Sampler slot: dedup by D3D12Sampler pointer. The GPU-level sampler cache (D3D12GPU::
  // samplerCache) already collapses identical SamplerDescriptors to a single shared D3D12Sampler
  // instance, so pointer identity is a sufficient key.
  D3D12_GPU_DESCRIPTOR_HANDLE sampGpu = {};
  auto sampIt = samplerSlotCache.find(d3d12Samp.get());
  if (sampIt != samplerSlotCache.end()) {
    sampGpu = sampIt->second;
  } else {
    if (samplerHeapHead >= samplerHeapCapacity) {
      LOGE("D3D12RenderPass::setTexture: Sampler heap exhausted (capacity=%u).",
           samplerHeapCapacity);
      return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE sampCpu = samplerHeap->GetCPUDescriptorHandleForHeapStart();
    sampCpu.ptr += static_cast<SIZE_T>(samplerHeapHead) * samplerDescriptorSize;
    sampGpu = samplerHeap->GetGPUDescriptorHandleForHeapStart();
    sampGpu.ptr += static_cast<UINT64>(samplerHeapHead) * samplerDescriptorSize;
    auto samplerDesc = d3d12Samp->samplerDesc();
    device->CreateSampler(&samplerDesc, sampCpu);
    samplerHeapHead++;
    samplerSlotCache.emplace(d3d12Samp.get(), sampGpu);
  }

  auto& tb = textureBindings[binding];
  tb.srvTableStart = srvGpu;
  tb.samplerTableStart = sampGpu;
  tb.dirty = true;
}

void D3D12RenderPass::flushBindingsIfNeeded() {
  if (!currentPipeline) {
    return;
  }
  // Apply uniform CBVs — one root constant buffer view per dirty uniform binding.
  for (unsigned i = 0; i < MaxUniformBindings; i++) {
    auto& ub = uniformBindings[i];
    if (!ub.dirty) {
      continue;
    }
    auto rootIndex = currentPipeline->getUniformRootParameterIndex(i);
    if (rootIndex == UINT32_MAX) {
      ub.dirty = false;
      continue;
    }
    commandList->SetGraphicsRootConstantBufferView(rootIndex, ub.gpuAddress);
    ub.dirty = false;
  }

  // Apply texture/sampler descriptor tables. Each texture binding occupies two consecutive root
  // parameters in our root signature: an SRV table (in the CBV/SRV/UAV heap) and a Sampler table
  // (in the Sampler heap). We bind both with separate SetGraphicsRootDescriptorTable calls.
  for (unsigned i = 0; i < MaxTextureBindings; i++) {
    auto& tb = textureBindings[i];
    if (!tb.dirty) {
      continue;
    }
    auto srvRoot = currentPipeline->getTextureRootParameterIndex(i);
    auto samplerRoot = currentPipeline->getSamplerRootParameterIndex(i);
    if (srvRoot != UINT32_MAX) {
      commandList->SetGraphicsRootDescriptorTable(srvRoot, tb.srvTableStart);
    }
    if (samplerRoot != UINT32_MAX) {
      commandList->SetGraphicsRootDescriptorTable(samplerRoot, tb.samplerTableStart);
    }
    tb.dirty = false;
  }
}

void D3D12RenderPass::setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                                      size_t offset) {
  if (!buffer || !currentPipeline) {
    return;
  }
  auto d3d12Buffer = std::static_pointer_cast<D3D12Buffer>(buffer);
  encoder->retainResource(d3d12Buffer);

  D3D12_VERTEX_BUFFER_VIEW view = {};
  view.BufferLocation = d3d12Buffer->d3d12Resource()->GetGPUVirtualAddress() + offset;
  view.SizeInBytes = static_cast<UINT>(d3d12Buffer->size() - offset);
  // D3D12 requires the per-vertex stride at draw time. We sourced it from the bound pipeline's
  // VertexBufferLayout when the pipeline was built. The Vulkan backend keeps stride implicit in
  // the VkPipeline's vertex input description; for D3D12 we must echo it back here.
  view.StrideInBytes = currentPipeline->getVertexStride(slot);
  commandList->IASetVertexBuffers(slot, 1, &view);
}

void D3D12RenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  if (!buffer) {
    return;
  }
  auto d3d12Buffer = std::static_pointer_cast<D3D12Buffer>(buffer);
  encoder->retainResource(d3d12Buffer);

  D3D12_INDEX_BUFFER_VIEW view = {};
  view.BufferLocation = d3d12Buffer->d3d12Resource()->GetGPUVirtualAddress();
  view.SizeInBytes = static_cast<UINT>(d3d12Buffer->size());
  view.Format = (format == IndexFormat::UInt32) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
  commandList->IASetIndexBuffer(&view);
}

void D3D12RenderPass::setStencilReference(uint32_t reference) {
  commandList->OMSetStencilRef(reference);
}

void D3D12RenderPass::draw(PrimitiveType primitiveType, uint32_t vertexCount,
                           uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
  if (!currentPipeline) {
    return;
  }
  auto topology = ToD3D12Topology(primitiveType);
  if (!primitiveTopologySet || topology != currentTopology) {
    commandList->IASetPrimitiveTopology(topology);
    currentTopology = topology;
    primitiveTopologySet = true;
  }
  flushBindingsIfNeeded();
  commandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void D3D12RenderPass::drawIndexed(PrimitiveType primitiveType, uint32_t indexCount,
                                  uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                                  uint32_t firstInstance) {
  if (!currentPipeline) {
    return;
  }
  auto topology = ToD3D12Topology(primitiveType);
  if (!primitiveTopologySet || topology != currentTopology) {
    commandList->IASetPrimitiveTopology(topology);
    currentTopology = topology;
    primitiveTopologySet = true;
  }
  flushBindingsIfNeeded();
  commandList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex,
                                    firstInstance);
}

void D3D12RenderPass::onEnd() {
  // Step 1: Resolve every multi-sampled colour attachment that has a resolveTexture into its
  // single-sample destination, mirroring Vulkan's pResolveAttachments. Driver state requirements
  // are RESOLVE_SOURCE for the multi-sample source and RESOLVE_DEST for the single-sample
  // destination; both are restored to COMMON afterwards in step 2.
  for (size_t i = 0; i < colorAttachments.size(); i++) {
    if (i >= resolveTextures.size() || resolveTextures[i] == nullptr) {
      continue;
    }
    auto& src = colorAttachments[i];
    auto& resolveDst = resolveTextures[i];
    if (src == nullptr) {
      continue;
    }

    auto srcState = src->currentState();
    if (srcState != D3D12_RESOURCE_STATE_RESOLVE_SOURCE) {
      TransitionResourceState(commandList, src->d3d12Resource(), srcState,
                              D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      src->setCurrentState(D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    }
    auto dstState = resolveDst->currentState();
    if (dstState != D3D12_RESOURCE_STATE_RESOLVE_DEST) {
      TransitionResourceState(commandList, resolveDst->d3d12Resource(), dstState,
                              D3D12_RESOURCE_STATE_RESOLVE_DEST);
      resolveDst->setCurrentState(D3D12_RESOURCE_STATE_RESOLVE_DEST);
    }
    commandList->ResolveSubresource(resolveDst->d3d12Resource(), 0, src->d3d12Resource(), 0,
                                    static_cast<DXGI_FORMAT>(src->dxgiFormat()));
  }

  // Step 2: Transition every color attachment back to COMMON. The next consumer (sample, copy,
  // present) will issue its own transition from COMMON to whatever state it needs.
  for (auto& tex : colorAttachments) {
    if (tex == nullptr) continue;
    auto current = tex->currentState();
    if (current != D3D12_RESOURCE_STATE_COMMON) {
      TransitionResourceState(commandList, tex->d3d12Resource(), current,
                              D3D12_RESOURCE_STATE_COMMON);
      tex->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
    }
  }
  for (auto& tex : resolveTextures) {
    if (tex == nullptr) continue;
    auto current = tex->currentState();
    if (current != D3D12_RESOURCE_STATE_COMMON) {
      TransitionResourceState(commandList, tex->d3d12Resource(), current,
                              D3D12_RESOURCE_STATE_COMMON);
      tex->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
    }
  }
  if (depthStencilAttachment != nullptr) {
    auto current = depthStencilAttachment->currentState();
    if (current != D3D12_RESOURCE_STATE_COMMON) {
      TransitionResourceState(commandList, depthStencilAttachment->d3d12Resource(), current,
                              D3D12_RESOURCE_STATE_COMMON);
      depthStencilAttachment->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
    }
  }
  // Transition every texture that was sampled (PIXEL_SHADER_RESOURCE) inside this pass back to
  // COMMON. D3D12 implicitly decays buffers and simultaneous-access textures to COMMON after the
  // command list executes; explicitly issuing the matching CPU-side transition keeps our tracker
  // aligned with the runtime so subsequent passes don't trip "Before state mismatch" barriers.
  for (auto& tex : shaderResourceTextures) {
    if (tex == nullptr) continue;
    auto current = tex->currentState();
    if (current != D3D12_RESOURCE_STATE_COMMON) {
      TransitionResourceState(commandList, tex->d3d12Resource(), current,
                              D3D12_RESOURCE_STATE_COMMON);
      tex->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
    }
  }
}

}  // namespace tgfx
