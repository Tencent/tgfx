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

#include "D3D12CommandEncoder.h"
#include "D3D12Buffer.h"
#include "D3D12CommandBuffer.h"
#include "D3D12Defines.h"
#include "D3D12GPU.h"
#include "D3D12RenderPass.h"
#include "D3D12Texture.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<D3D12CommandEncoder> D3D12CommandEncoder::Make(D3D12GPU* gpu) {
  if (gpu == nullptr) {
    return nullptr;
  }
  auto device = gpu->device();
  ComPtr<ID3D12CommandAllocator> allocator = nullptr;
  auto hr =
      device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
  if (FAILED(hr)) {
    LOGE("D3D12CommandEncoder: CreateCommandAllocator failed, HRESULT=0x%08X",
         static_cast<unsigned>(hr));
    return nullptr;
  }

  ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
  hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                 IID_PPV_ARGS(&commandList));
  if (FAILED(hr)) {
    LOGE("D3D12CommandEncoder: CreateCommandList failed, HRESULT=0x%08X",
         static_cast<unsigned>(hr));
    return nullptr;
  }
  // The command list returned by CreateCommandList is already in the recording state, matching
  // VulkanCommandEncoder's vkBeginCommandBuffer.

  return gpu->makeResource<D3D12CommandEncoder>(gpu, std::move(allocator), std::move(commandList));
}

D3D12CommandEncoder::D3D12CommandEncoder(D3D12GPU* gpu, ComPtr<ID3D12CommandAllocator> allocator,
                                         ComPtr<ID3D12GraphicsCommandList> commandList)
    : _gpu(gpu) {
  session.commandAllocator = std::move(allocator);
  session.commandList = std::move(commandList);
}

GPU* D3D12CommandEncoder::gpu() const {
  return _gpu;
}

std::shared_ptr<RenderPass> D3D12CommandEncoder::onBeginRenderPass(
    const RenderPassDescriptor& descriptor) {
  return D3D12RenderPass::Make(this, descriptor);
}

void D3D12CommandEncoder::copyTextureToTexture(std::shared_ptr<Texture> srcTexture,
                                               const Rect& srcRect,
                                               std::shared_ptr<Texture> dstTexture,
                                               const Point& dstOffset) {
  if (!srcTexture || !dstTexture) {
    return;
  }
  // Clamp copy region to source bounds.
  auto srcX = static_cast<int32_t>(srcRect.x());
  auto srcY = static_cast<int32_t>(srcRect.y());
  auto copyWidth = static_cast<uint32_t>(srcRect.width());
  auto copyHeight = static_cast<uint32_t>(srcRect.height());
  auto srcW = static_cast<uint32_t>(srcTexture->width());
  auto srcH = static_cast<uint32_t>(srcTexture->height());
  if (srcX + copyWidth > srcW) {
    copyWidth = srcW > static_cast<uint32_t>(srcX) ? srcW - static_cast<uint32_t>(srcX) : 0;
  }
  if (srcY + copyHeight > srcH) {
    copyHeight = srcH > static_cast<uint32_t>(srcY) ? srcH - static_cast<uint32_t>(srcY) : 0;
  }

  auto d3d12Src = std::static_pointer_cast<D3D12Texture>(srcTexture);
  auto d3d12Dst = std::static_pointer_cast<D3D12Texture>(dstTexture);
  retainResource(d3d12Src);
  retainResource(d3d12Dst);

  auto cmd = session.commandList.Get();
  if (copyWidth == 0 || copyHeight == 0) {
    return;
  }

  TransitionResourceState(cmd, d3d12Src->d3d12Resource(), d3d12Src->currentState(),
                          D3D12_RESOURCE_STATE_COPY_SOURCE);
  TransitionResourceState(cmd, d3d12Dst->d3d12Resource(), d3d12Dst->currentState(),
                          D3D12_RESOURCE_STATE_COPY_DEST);

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = d3d12Dst->d3d12Resource();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource = d3d12Src->d3d12Resource();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;

  D3D12_BOX srcBox = {};
  srcBox.left = static_cast<UINT>(srcX);
  srcBox.top = static_cast<UINT>(srcY);
  srcBox.front = 0;
  srcBox.right = static_cast<UINT>(srcX) + copyWidth;
  srcBox.bottom = static_cast<UINT>(srcY) + copyHeight;
  srcBox.back = 1;

  cmd->CopyTextureRegion(&dst, static_cast<UINT>(dstOffset.x), static_cast<UINT>(dstOffset.y), 0,
                         &src, &srcBox);

  // Transition back to a generic shader-readable state. We use COMMON, which D3D12 will promote
  // implicitly to PIXEL_SHADER_RESOURCE the next time the texture is sampled — matching the
  // "promote on demand" behaviour the rest of the backend assumes.
  TransitionResourceState(cmd, d3d12Src->d3d12Resource(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                          D3D12_RESOURCE_STATE_COMMON);
  TransitionResourceState(cmd, d3d12Dst->d3d12Resource(), D3D12_RESOURCE_STATE_COPY_DEST,
                          D3D12_RESOURCE_STATE_COMMON);
  d3d12Src->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
  d3d12Dst->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
}

void D3D12CommandEncoder::copyTextureToBuffer(std::shared_ptr<Texture> srcTexture,
                                              const Rect& srcRect,
                                              std::shared_ptr<GPUBuffer> dstBuffer,
                                              size_t dstOffset, size_t dstRowBytes) {
  if (!srcTexture || !dstBuffer) {
    return;
  }
  auto srcX = static_cast<int32_t>(srcRect.x());
  auto srcY = static_cast<int32_t>(srcRect.y());
  auto copyWidth = static_cast<uint32_t>(srcRect.width());
  auto copyHeight = static_cast<uint32_t>(srcRect.height());
  auto srcW = static_cast<uint32_t>(srcTexture->width());
  auto srcH = static_cast<uint32_t>(srcTexture->height());
  if (srcX + copyWidth > srcW) {
    copyWidth = srcW > static_cast<uint32_t>(srcX) ? srcW - static_cast<uint32_t>(srcX) : 0;
  }
  if (srcY + copyHeight > srcH) {
    copyHeight = srcH > static_cast<uint32_t>(srcY) ? srcH - static_cast<uint32_t>(srcY) : 0;
  }
  if (copyWidth == 0 || copyHeight == 0) {
    return;
  }

  auto d3d12Src = std::static_pointer_cast<D3D12Texture>(srcTexture);
  auto d3d12Dst = std::static_pointer_cast<D3D12Buffer>(dstBuffer);
  retainResource(d3d12Src);
  retainResource(d3d12Dst);

  auto cmd = session.commandList.Get();
  auto bytesPerPixel = static_cast<uint32_t>(DXGIFormatBytesPerPixel(d3d12Src->dxgiFormat()));
  uint32_t tightRowBytes =
      dstRowBytes > 0 ? static_cast<uint32_t>(dstRowBytes) : copyWidth * bytesPerPixel;

  // D3D12 requires CopyTextureRegion's destination buffer footprint to use a row pitch that is a
  // multiple of D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256). The caller's buffer is laid out tightly
  // (one row immediately follows the previous), so when tightRowBytes happens to be unaligned we
  // route the copy through a transient default-heap staging buffer with an aligned row pitch and
  // then issue per-row CopyBufferRegion calls to repack the rows into the caller's buffer.
  uint32_t alignedRowPitch = (tightRowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                             ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
  bool needsRepack = (alignedRowPitch != tightRowBytes);

  TransitionResourceState(cmd, d3d12Src->d3d12Resource(), d3d12Src->currentState(),
                          D3D12_RESOURCE_STATE_COPY_SOURCE);

  ComPtr<ID3D12Resource> stagingBuffer = nullptr;
  ID3D12Resource* footprintTarget = d3d12Dst->d3d12Resource();
  UINT64 footprintOffset = static_cast<UINT64>(dstOffset);

  if (needsRepack) {
    // Allocate a transient default-heap buffer big enough to hold the aligned-row-pitch image.
    // The buffer is created in COPY_DEST state so CopyTextureRegion can write to it directly,
    // then transitioned to COPY_SOURCE so we can read it back row-by-row into the caller's
    // buffer. The session retains the staging buffer until the fence signals.
    auto device = _gpu->device();
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = static_cast<UINT64>(alignedRowPitch) * copyHeight;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = static_cast<DXGI_FORMAT>(0);  // DXGI_FORMAT_UNKNOWN
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    auto hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&stagingBuffer));
    if (FAILED(hr)) {
      LOGE("D3D12CommandEncoder::copyTextureToBuffer: staging buffer creation failed, "
           "HRESULT=0x%08X",
           static_cast<unsigned>(hr));
      // Fall back to direct copy with potentially wrong stride; better than dropping silently.
      stagingBuffer = nullptr;
      needsRepack = false;
    } else {
      footprintTarget = stagingBuffer.Get();
      footprintOffset = 0;
    }
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  footprint.Offset = footprintOffset;
  footprint.Footprint.Format = static_cast<DXGI_FORMAT>(d3d12Src->dxgiFormat());
  footprint.Footprint.Width = copyWidth;
  footprint.Footprint.Height = copyHeight;
  footprint.Footprint.Depth = 1;
  footprint.Footprint.RowPitch = needsRepack ? alignedRowPitch : tightRowBytes;

  D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
  dstLoc.pResource = footprintTarget;
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dstLoc.PlacedFootprint = footprint;

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = d3d12Src->d3d12Resource();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  srcLoc.SubresourceIndex = 0;

  D3D12_BOX srcBox = {};
  srcBox.left = static_cast<UINT>(srcX);
  srcBox.top = static_cast<UINT>(srcY);
  srcBox.front = 0;
  srcBox.right = static_cast<UINT>(srcX) + copyWidth;
  srcBox.bottom = static_cast<UINT>(srcY) + copyHeight;
  srcBox.back = 1;

  cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);

  if (needsRepack) {
    // Transition the staging buffer to COPY_SOURCE and repack each row into the caller's buffer.
    TransitionResourceState(cmd, stagingBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (uint32_t row = 0; row < copyHeight; row++) {
      cmd->CopyBufferRegion(d3d12Dst->d3d12Resource(),
                            static_cast<UINT64>(dstOffset) + row * tightRowBytes,
                            stagingBuffer.Get(), row * alignedRowPitch, tightRowBytes);
    }
    session.auxBuffers.push_back(std::move(stagingBuffer));
  }

  TransitionResourceState(cmd, d3d12Src->d3d12Resource(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                          D3D12_RESOURCE_STATE_COMMON);
  d3d12Src->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
}

void D3D12CommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  if (!texture) {
    return;
  }
  // D3D12 has no built-in equivalent to vkCmdBlitImage. A correct implementation requires either
  // a compute shader doing per-level downsampling or a small graphics pipeline. We log a warning
  // here so the omission is visible; the few rendering paths that depend on mip generation will
  // need follow-up work.
  if (texture->mipLevelCount() > 1) {
    LOGE(
        "D3D12CommandEncoder::generateMipmapsForTexture: mipmap generation is not yet "
        "implemented for the D3D12 backend.");
  }
}

std::shared_ptr<CommandBuffer> D3D12CommandEncoder::onFinish() {
  auto hr = session.commandList->Close();
  if (FAILED(hr)) {
    LOGE("D3D12CommandEncoder: ID3D12GraphicsCommandList::Close failed, HRESULT=0x%08X",
         static_cast<unsigned>(hr));
    _gpu->reclaimAbandonedSession(std::move(session));
    return nullptr;
  }
  return std::make_shared<D3D12CommandBuffer>(_gpu, std::move(session));
}

void D3D12CommandEncoder::onRelease(D3D12GPU* gpu) {
  // If onFinish() was called, the session has already been moved to D3D12CommandBuffer.
  // This path only handles abandoned encoders (encoding was started but never finished).
  if (session.commandList == nullptr) {
    return;
  }
  gpu->reclaimAbandonedSession(std::move(session));
}

}  // namespace tgfx
