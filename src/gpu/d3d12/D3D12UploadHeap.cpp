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

#include "D3D12UploadHeap.h"
#include "core/utils/Log.h"

namespace tgfx {

static size_t AlignUpSize(size_t value, size_t alignment) {
  if (alignment <= 1) {
    return value;
  }
  return (value + alignment - 1) & ~(alignment - 1);
}

bool D3D12UploadHeap::init(ID3D12Device* device, size_t capacity) {
  if (device == nullptr || capacity == 0) {
    return false;
  }
  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = static_cast<UINT64>(capacity);
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = static_cast<DXGI_FORMAT>(DXGI_FORMAT_UNKNOWN);
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  ComPtr<ID3D12Resource> resource = nullptr;
  auto hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&resource));
  if (FAILED(hr)) {
    LOGE("D3D12UploadHeap::init(): CreateCommittedResource failed (HRESULT=0x%08X size=%zu).",
         static_cast<unsigned>(hr), capacity);
    return false;
  }

  void* mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  hr = resource->Map(0, &readRange, &mapped);
  if (FAILED(hr) || mapped == nullptr) {
    LOGE("D3D12UploadHeap::init(): Map failed (HRESULT=0x%08X).", static_cast<unsigned>(hr));
    return false;
  }

  _resource = std::move(resource);
  mappedCpu = mapped;
  gpuVA = _resource->GetGPUVirtualAddress();
  _capacity = capacity;
  head = 0;
  committedHead = 0;
  outstandingBytes = 0;
  // Drop any inflight entries left over from a previous init() so the post-init state really is
  // "fresh", matching the resetForContextLost() invariant. There is no current re-init path,
  // but if one is added later (device-lost recovery, test teardown) those inflight entries
  // would otherwise reference the previous, just-released resource.
  inflight.clear();
  return true;
}

D3D12UploadHeap::Allocation D3D12UploadHeap::allocate(size_t size, size_t alignment) {
  if (_resource == nullptr || size == 0 || size > _capacity) {
    return {};
  }
  size_t alignedHead = AlignUpSize(head, alignment);
  // Track how much of the ring is currently occupied by outstanding allocations. head alone
  // cannot disambiguate "ring empty" from "ring full" because both produce the same value once
  // an allocation wraps it; we add an explicit byte counter that is incremented on every
  // allocate() and decremented on retire(). This is what stops a wrap from silently overwriting
  // staging bytes that were allocated but not yet committed (see RecordingTest race details
  // captured in commit notes).
  size_t free = _capacity - outstandingBytes;
  size_t needed = size;
  size_t startOffset = alignedHead;
  size_t skipped = 0;
  if (alignedHead + size > _capacity) {
    // Splitting the range across the wrap boundary is not supported (CopyTextureRegion needs a
    // single contiguous PLACED_SUBRESOURCE_FOOTPRINT), so jump back to offset 0 and pay for the
    // discarded bytes between head and the end of the ring out of the same free pool.
    skipped = _capacity - head;
    needed = size + skipped;
    startOffset = 0;
  } else {
    needed = size + (alignedHead - head);
  }
  if (needed > free) {
    return {};
  }

  Allocation result = {};
  result.resource = _resource.Get();
  result.cpu = static_cast<uint8_t*>(mappedCpu) + startOffset;
  result.gpuVirtualAddress = gpuVA + startOffset;
  result.offsetInResource = startOffset;
  result.size = size;
  head = startOffset + size;
  if (head == _capacity) {
    head = 0;
  }
  outstandingBytes += needed;
  return result;
}

void D3D12UploadHeap::commit(uint64_t fenceValue) {
  // Pair the about-to-be-signalled fence with the bytes consumed since the last commit so
  // retire() can give those bytes back when the GPU finishes with them. Compute the byte total
  // first because the fast `head == committedHead` check is ambiguous: it triggers both when
  // truly nothing was allocated and when a single allocation spanned the entire capacity and
  // wrapped head right back to committedHead.
  size_t bytesSinceCommit =
      (head >= committedHead) ? (head - committedHead) : (_capacity - (committedHead - head));
  if (bytesSinceCommit == 0) {
    if (outstandingBytes == 0) {
      return;
    }
    // Whole-capacity allocation case — bill the full ring to this fence so the retire() path
    // eventually drains outstandingBytes. Without this branch the bytes would leak and stop
    // the ring from accepting any further allocations once outstandingBytes saturates.
    bytesSinceCommit = _capacity;
  }
  InflightRange entry = {};
  entry.fenceValue = fenceValue;
  entry.bytes = bytesSinceCommit;
  inflight.push_back(entry);
  committedHead = head;
}

void D3D12UploadHeap::retire(uint64_t completedFenceValue) {
  while (!inflight.empty() && inflight.front().fenceValue <= completedFenceValue) {
    if (outstandingBytes >= inflight.front().bytes) {
      outstandingBytes -= inflight.front().bytes;
    } else {
      // Defensive: bookkeeping should never drop below zero, but if it does we reset rather
      // than wrap to ~0 and stop accepting allocations forever.
      outstandingBytes = 0;
    }
    inflight.pop_front();
  }
}

void D3D12UploadHeap::clear() {
  if (_resource != nullptr) {
    _resource->Unmap(0, nullptr);
  }
  _resource = nullptr;
  mappedCpu = nullptr;
  gpuVA = 0;
  _capacity = 0;
  head = 0;
  committedHead = 0;
  outstandingBytes = 0;
  inflight.clear();
}

void D3D12UploadHeap::resetForContextLost() {
  // Keep _resource / mappedCpu / gpuVA / _capacity intact; the ring stays usable. Just drop
  // every accounting entry that is waiting on a fence that is never going to advance.
  head = 0;
  committedHead = 0;
  outstandingBytes = 0;
  inflight.clear();
}

}  // namespace tgfx
