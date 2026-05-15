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
  tail = 0;
  committedHead = 0;
  return true;
}

D3D12UploadHeap::Allocation D3D12UploadHeap::allocate(size_t size, size_t alignment) {
  if (_resource == nullptr || size == 0 || size > _capacity) {
    return {};
  }
  size_t alignedHead = AlignUpSize(head, alignment);
  // Available room without crossing tail; tail == head means the ring is empty.
  size_t used = (head >= tail) ? (head - tail) : (_capacity - (tail - head));
  size_t free = _capacity - used;
  // Skip over the wrap boundary if a contiguous range cannot be carved out at the tail end of
  // the buffer. Splitting the range across the boundary would force callers to issue two
  // CopyTextureRegion calls per upload, which D3D12 does not naturally support for a single
  // PLACED_SUBRESOURCE_FOOTPRINT.
  size_t startOffset = alignedHead;
  if (alignedHead + size > _capacity) {
    size_t skipped = _capacity - head;
    if (size + skipped > free) {
      return {};
    }
    startOffset = 0;
    used += skipped;
    free -= skipped;
  } else if (alignedHead - head > free - size) {
    // Aligning consumes some bytes that fall after head; ensure they fit before allocating.
    return {};
  } else {
    used += alignedHead - head;
    free -= alignedHead - head;
  }
  if (size > free) {
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
  return result;
}

void D3D12UploadHeap::commit(uint64_t fenceValue) {
  if (head == committedHead) {
    return;
  }
  InflightRange entry = {};
  entry.fenceValue = fenceValue;
  entry.newHead = head;
  inflight.push_back(entry);
  committedHead = head;
}

void D3D12UploadHeap::retire(uint64_t completedFenceValue) {
  while (!inflight.empty() && inflight.front().fenceValue <= completedFenceValue) {
    tail = inflight.front().newHead;
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
  tail = 0;
  committedHead = 0;
  inflight.clear();
}

}  // namespace tgfx
