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

#include "D3D12CommandQueue.h"
#include "D3D12Buffer.h"
#include "D3D12CommandBuffer.h"
#include "D3D12Defines.h"
#include "D3D12Semaphore.h"
#include "D3D12Texture.h"
#include "core/utils/Log.h"

namespace tgfx {

template <typename T>
static T AlignUp(T x, T alignment) {
  return (x + alignment - 1) & ~(alignment - 1);
}

D3D12CommandQueue::D3D12CommandQueue(D3D12GPU* d3d12GPU) : gpu(d3d12GPU) {
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  auto hr = gpu->device()->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue));
  if (FAILED(hr)) {
    LOGE("D3D12CommandQueue: Failed to create command queue, HRESULT=0x%08X",
         static_cast<unsigned>(hr));
  }
}

D3D12CommandQueue::~D3D12CommandQueue() {
  // Pending uploads (staging buffers + footprints) and pending semaphores will be released by
  // the field destructors. There is no command list to flush them with at this point — the
  // application must call waitUntilCompleted() before destruction if it cares about durability.
}

std::chrono::steady_clock::time_point D3D12CommandQueue::completedFrameTime() const {
  return gpu->lastFenceSignalTime();
}

void D3D12CommandQueue::writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset,
                                    const void* data, size_t dataSize) {
  if (!buffer || !data || dataSize == 0) {
    return;
  }
  void* mappedData = buffer->map(bufferOffset, dataSize);
  if (mappedData) {
    memcpy(mappedData, data, dataSize);
    buffer->unmap();
  }
}

void D3D12CommandQueue::writeTexture(std::shared_ptr<Texture> texture, const Rect& rect,
                                     const void* pixels, size_t rowBytes) {
  if (!texture || !pixels) {
    return;
  }
  auto d3d12Tex = std::static_pointer_cast<D3D12Texture>(texture);

  auto width = static_cast<uint32_t>(rect.width());
  auto height = static_cast<uint32_t>(rect.height());
  auto bytesPerPixel = static_cast<uint32_t>(DXGIFormatBytesPerPixel(d3d12Tex->dxgiFormat()));
  if (width == 0 || height == 0 || bytesPerPixel == 0) {
    return;
  }

  // D3D12 requires the row pitch of a placed footprint to be a multiple of
  // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256). Caller-supplied stride may be larger or smaller.
  uint32_t srcRowBytes = rowBytes > 0 ? static_cast<uint32_t>(rowBytes) : width * bytesPerPixel;
  uint32_t alignedRowPitch = AlignUp<uint32_t>(
      width * bytesPerPixel, static_cast<uint32_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
  uint64_t stagingSize = static_cast<uint64_t>(alignedRowPitch) * height;

  // Fast path: sub-allocate from the GPU's process-wide UPLOAD ring. The ring resource is kept
  // alive by D3D12GPU and the bytes are reclaimed automatically once the owning fence signals,
  // so we do not need to add anything to PendingUpload to keep the resource live.
  ID3D12Resource* stagingResource = nullptr;
  uint64_t stagingOffset = 0;
  uint8_t* stagingCpu = nullptr;
  ComPtr<ID3D12Resource> fallbackResource = nullptr;
  auto allocation =
      gpu->uploadHeap().allocate(static_cast<size_t>(stagingSize),
                                 static_cast<size_t>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT));
  if (allocation.valid()) {
    stagingResource = allocation.resource;
    stagingOffset = allocation.offsetInResource;
    stagingCpu = static_cast<uint8_t*>(allocation.cpu);
  } else {
    // Slow path (oversize allocation or saturated ring): create a one-off UPLOAD buffer. Its
    // ComPtr is parked in PendingUpload so the resource outlives GPU execution.
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = stagingSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = static_cast<DXGI_FORMAT>(DXGI_FORMAT_UNKNOWN);
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    auto hr = gpu->device()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&fallbackResource));
    if (FAILED(hr)) {
      LOGE(
          "D3D12CommandQueue::writeTexture: fallback CreateCommittedResource failed, "
          "HRESULT=0x%08X",
          static_cast<unsigned>(hr));
      return;
    }
    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    hr = fallbackResource->Map(0, &readRange, &mapped);
    if (FAILED(hr) || mapped == nullptr) {
      LOGE("D3D12CommandQueue::writeTexture: fallback Map failed, HRESULT=0x%08X",
           static_cast<unsigned>(hr));
      return;
    }
    stagingResource = fallbackResource.Get();
    stagingOffset = 0;
    stagingCpu = static_cast<uint8_t*>(mapped);
  }

  auto src = static_cast<const uint8_t*>(pixels);
  uint32_t tightRowBytes = width * bytesPerPixel;
  for (uint32_t row = 0; row < height; row++) {
    memcpy(stagingCpu + row * alignedRowPitch, src + row * srcRowBytes, tightRowBytes);
  }
  if (fallbackResource != nullptr) {
    // Mapping a one-off UPLOAD buffer for the duration of GPU execution is allowed but we Unmap
    // here for symmetry with the previous (pre-ring) implementation; this also lets the runtime
    // page out the buffer if memory pressure allows.
    fallbackResource->Unmap(0, nullptr);
  }

  D3D12GPU::PendingUpload upload = {};
  // Only the slow path needs to retain the staging buffer: the ring resource lives on the GPU
  // instance and is reclaimed by fence directly.
  upload.stagingBuffer = std::move(fallbackResource);
  upload.texture = d3d12Tex;
  pendingUploads.push_back(std::move(upload));

  UploadFootprint fp = {};
  fp.stagingResource = stagingResource;
  fp.footprint.Offset = stagingOffset;
  fp.footprint.Footprint.Format = static_cast<DXGI_FORMAT>(d3d12Tex->dxgiFormat());
  fp.footprint.Footprint.Width = width;
  fp.footprint.Footprint.Height = height;
  fp.footprint.Footprint.Depth = 1;
  fp.footprint.Footprint.RowPitch = alignedRowPitch;
  fp.dstX = static_cast<UINT>(rect.x());
  fp.dstY = static_cast<UINT>(rect.y());
  fp.srcWidth = width;
  fp.srcHeight = height;
  pendingFootprints.push_back(fp);
}

void D3D12CommandQueue::flushUploads(ID3D12GraphicsCommandList* commandList) {
  if (pendingUploads.empty() || commandList == nullptr) {
    return;
  }
  for (size_t i = 0; i < pendingUploads.size(); i++) {
    auto& up = pendingUploads[i];
    auto& fp = pendingFootprints[i];

    auto current = up.texture->currentState();
    if (current != D3D12_RESOURCE_STATE_COPY_DEST) {
      TransitionResourceState(commandList, up.texture->d3d12Resource(), current,
                              D3D12_RESOURCE_STATE_COPY_DEST);
      up.texture->setCurrentState(D3D12_RESOURCE_STATE_COPY_DEST);
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = up.texture->d3d12Resource();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    // The staging source is either a slot inside the GPU's UPLOAD ring (kept alive by the GPU
    // instance, with offsetInResource embedded in fp.footprint.Offset) or a one-off staging
    // buffer parked in PendingUpload::stagingBuffer. Either way fp.stagingResource is the raw
    // pointer to use at copy time.
    srcLoc.pResource = fp.stagingResource;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = fp.footprint;

    D3D12_BOX srcBox = {};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = fp.srcWidth;
    srcBox.bottom = fp.srcHeight;
    srcBox.back = 1;

    commandList->CopyTextureRegion(&dstLoc, fp.dstX, fp.dstY, 0, &srcLoc, &srcBox);

    TransitionResourceState(commandList, up.texture->d3d12Resource(),
                            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    up.texture->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
  }
  pendingFootprints.clear();
  // pendingUploads is moved into the SubmitRequest by the caller so its staging buffers outlive
  // GPU execution.
}

void D3D12CommandQueue::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
  if (!commandBuffer) {
    return;
  }
  auto d3d12Cmd = std::static_pointer_cast<D3D12CommandBuffer>(commandBuffer);
  auto session = std::move(d3d12Cmd->frameSession());
  if (session.commandList == nullptr) {
    return;
  }

  // If pixel uploads were recorded since the last submit, splice them onto the front of the
  // submission as an auxiliary upload command list. The GPU executes auxCommandLists before the
  // session.commandList, ensuring textures are populated before the render list samples them.
  if (!pendingUploads.empty()) {
    auto entry = gpu->commandListPool().acquire(gpu->device());
    if (!entry.valid()) {
      LOGE(
          "D3D12CommandQueue::submit: failed to acquire transient upload list, dropping "
          "uploads.");
      pendingUploads.clear();
      pendingFootprints.clear();
    } else {
      flushUploads(entry.commandList.Get());
      entry.commandList->Close();
      session.auxAllocators.push_back(std::move(entry.allocator));
      session.auxCommandLists.push_back(std::move(entry.commandList));
    }
  }

  D3D12GPU::SubmitRequest request = {};
  request.session = std::move(session);
  request.uploads = std::move(pendingUploads);
  request.signalSemaphore = std::move(pendingSignalSemaphore);
  request.waitSemaphore = std::move(pendingWaitSemaphore);
  // Capture _frameTime here (CommandQueue base class member). The GPU stamps the inflight
  // submission with this value and later publishes it as _lastFenceSignalTime so the resource
  // cache can decide which scratch resources the GPU is done reading.
  request.frameTime = _frameTime;
  pendingUploads.clear();
  pendingFootprints.clear();
  pendingSignalSemaphore = nullptr;
  pendingWaitSemaphore = nullptr;

  gpu->executeSubmission(std::move(request));
}

std::shared_ptr<Semaphore> D3D12CommandQueue::insertSemaphore() {
  auto semaphore = D3D12Semaphore::Make(gpu);
  if (semaphore == nullptr) {
    return nullptr;
  }
  pendingSignalSemaphore = semaphore;
  return semaphore;
}

void D3D12CommandQueue::waitSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr) {
    return;
  }
  pendingWaitSemaphore = std::static_pointer_cast<D3D12Semaphore>(semaphore);
}

void D3D12CommandQueue::waitUntilCompleted() {
  // Flush any pending uploads even if the application did not submit a command buffer between
  // writeTexture() and waitUntilCompleted().
  if (!pendingUploads.empty()) {
    auto entry = gpu->commandListPool().acquire(gpu->device());
    if (entry.valid()) {
      flushUploads(entry.commandList.Get());
      entry.commandList->Close();

      D3D12GPU::SubmitRequest request = {};
      request.session.auxAllocators.push_back(std::move(entry.allocator));
      request.session.auxCommandLists.push_back(std::move(entry.commandList));
      request.uploads = std::move(pendingUploads);
      request.frameTime = _frameTime;
      pendingUploads.clear();
      pendingFootprints.clear();
      gpu->executeSubmission(std::move(request));
    } else {
      LOGE(
          "D3D12CommandQueue::waitUntilCompleted: failed to acquire upload list, dropping "
          "uploads.");
      pendingUploads.clear();
      pendingFootprints.clear();
    }
  }
  gpu->waitAllInflightSubmissions();
}

}  // namespace tgfx
