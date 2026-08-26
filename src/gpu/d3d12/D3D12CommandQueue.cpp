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
#include <unordered_set>
#include "D3D12BarrierBatch.h"
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

  // Clamp copy region to destination texture bounds, matching copyTextureToTexture. Negative
  // origins are clamped to 0 before any unsigned arithmetic; leaving dstX/dstY signed would cause
  // dstX + copyWidth to wrap through uint32 (e.g. dstX=-10, copyWidth=100 wraps to 90) and the
  // out-of-range check to silently pass, later feeding ~4G into CopyTextureRegion.
  auto dstX = rect.x() < 0 ? 0u : static_cast<uint32_t>(rect.x());
  auto dstY = rect.y() < 0 ? 0u : static_cast<uint32_t>(rect.y());
  auto width = static_cast<uint32_t>(rect.width());
  auto height = static_cast<uint32_t>(rect.height());
  auto texW = static_cast<uint32_t>(d3d12Tex->width());
  auto texH = static_cast<uint32_t>(d3d12Tex->height());

  auto copyWidth = width;
  auto copyHeight = height;
  if (dstX + copyWidth > texW) {
    copyWidth = texW > dstX ? texW - dstX : 0;
  }
  if (dstY + copyHeight > texH) {
    copyHeight = texH > dstY ? texH - dstY : 0;
  }

  auto bytesPerPixel = static_cast<uint32_t>(DXGIFormatBytesPerPixel(d3d12Tex->dxgiFormat()));
  if (copyWidth == 0 || copyHeight == 0 || bytesPerPixel == 0) {
    return;
  }

  // D3D12 requires the row pitch of a placed footprint to be a multiple of
  // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256). Caller-supplied stride may be larger or smaller.
  // srcRowBytes uses the original width (not clamped) as the source pixel layout matches the
  // caller's full rect dimensions.
  uint32_t srcRowBytes = rowBytes > 0 ? static_cast<uint32_t>(rowBytes) : width * bytesPerPixel;
  uint32_t alignedRowPitch = AlignUp<uint32_t>(
      copyWidth * bytesPerPixel, static_cast<uint32_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
  uint64_t stagingSize = static_cast<uint64_t>(alignedRowPitch) * copyHeight;

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
  uint32_t tightRowBytes = copyWidth * bytesPerPixel;
  for (uint32_t row = 0; row < copyHeight; row++) {
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
  fp.footprint.Footprint.Width = copyWidth;
  fp.footprint.Footprint.Height = copyHeight;
  fp.footprint.Footprint.Depth = 1;
  fp.footprint.Footprint.RowPitch = alignedRowPitch;
  fp.dstX = static_cast<UINT>(dstX);
  fp.dstY = static_cast<UINT>(dstY);
  fp.srcWidth = copyWidth;
  fp.srcHeight = copyHeight;
  pendingFootprints.push_back(fp);
}

void D3D12CommandQueue::flushUploads(ID3D12GraphicsCommandList* commandList,
                                     D3D12FrameSession& session) {
  if (pendingUploads.empty() || commandList == nullptr) {
    return;
  }
  // Batch transitions per unique texture: N uploads into the same atlas otherwise produce 2N
  // ResourceBarrier calls, since the exit transition back to COMMON re-triggers the entry
  // transition on the next upload (see AtlasUploadTask which uploads one cell per glyph).
  D3D12BarrierBatch enterBatch;
  std::unordered_set<D3D12Texture*> touchedTextures;
  for (auto& up : pendingUploads) {
    auto* tex = up.texture.get();
    if (!touchedTextures.insert(tex).second) {
      continue;
    }
    auto current = tex->currentState();
    enterBatch.addTransition(tex->d3d12Resource(), current, D3D12_RESOURCE_STATE_COPY_DEST);
    // Snapshot the CPU-tracked state on first touch so reclaimAbandonedSession can roll back
    // if these copies never reach the GPU. emplace() preserves the earliest snapshot.
    session.initialTextureStates.emplace(tex, current);
    tex->setCurrentState(D3D12_RESOURCE_STATE_COPY_DEST);
  }
  enterBatch.flush(commandList);

  for (size_t i = 0; i < pendingUploads.size(); i++) {
    auto& up = pendingUploads[i];
    auto& fp = pendingFootprints[i];

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = up.texture->d3d12Resource();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    // SubresourceIndex 0 = (mip level 0, array slice 0, plane 0). This matches the public
    // writeTexture contract in CommandQueue.h: "If the texture has mipmaps, you should call
    // CommandEncoder's generateMipmapsForTexture() method after writing the pixels, as mipmaps
    // will not be generated automatically." VulkanCommandQueue / MetalCommandQueue make the
    // same assumption (Vulkan even DEBUG_ASSERTs imageSubresource.mipLevel == 0 in its upload
    // batcher). If tgfx ever adds array textures or a per-mip writeTexture overload, every
    // backend must extend together — this is not a D3D12-only TODO.
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
  }

  D3D12BarrierBatch exitBatch;
  for (auto* tex : touchedTextures) {
    exitBatch.addTransition(tex->d3d12Resource(), D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_COMMON);
    tex->setCurrentState(D3D12_RESOURCE_STATE_COMMON);
  }
  exitBatch.flush(commandList);

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
      flushUploads(entry.commandList.Get(), session);
      // Close() failure typically means the recorded commands are malformed or the device is
      // removed. Submitting an unclosed list to ExecuteCommandLists is a hard driver error, so
      // drop the aux list, log, and fall through to submitting only the main render list. The
      // pending uploads are discarded because their staging bytes were charged against the
      // upload ring under the assumption they would be consumed by this submission.
      auto closeHr = entry.commandList->Close();
      if (FAILED(closeHr)) {
        LOGE(
            "D3D12CommandQueue::submit: transient upload list Close failed (HRESULT=0x%08X), "
            "dropping uploads.",
            static_cast<unsigned>(closeHr));
        pendingUploads.clear();
        pendingFootprints.clear();
      } else {
        session.auxAllocators.push_back(std::move(entry.allocator));
        session.auxCommandLists.push_back(std::move(entry.commandList));
      }
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
      D3D12GPU::SubmitRequest request = {};
      flushUploads(entry.commandList.Get(), request.session);
      auto closeHr = entry.commandList->Close();
      if (FAILED(closeHr)) {
        // See the identical rationale in submit(): submitting an unclosed command list is a
        // driver-level error, so drop the aux list and its pending uploads. Nothing to fall
        // through to here — this branch only runs when there is no active session.
        LOGE(
            "D3D12CommandQueue::waitUntilCompleted: upload list Close failed (HRESULT=0x%08X), "
            "dropping uploads.",
            static_cast<unsigned>(closeHr));
        pendingUploads.clear();
        pendingFootprints.clear();
      } else {
        request.session.auxAllocators.push_back(std::move(entry.allocator));
        request.session.auxCommandLists.push_back(std::move(entry.commandList));
        request.uploads = std::move(pendingUploads);
        request.frameTime = _frameTime;
        pendingUploads.clear();
        pendingFootprints.clear();
        gpu->executeSubmission(std::move(request));
      }
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
