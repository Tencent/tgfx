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

#include "D3D12Buffer.h"
#include "D3D12GPU.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<D3D12Buffer> D3D12Buffer::Make(D3D12GPU* gpu, size_t size, uint32_t usage) {
  if (gpu == nullptr || size == 0) {
    return nullptr;
  }

  D3D12_HEAP_PROPERTIES heapProperties = {};
  if (usage & GPUBufferUsage::READBACK) {
    heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
  } else {
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
  }

  D3D12_RESOURCE_DESC resourceDesc = {};
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resourceDesc.Width = static_cast<UINT64>(size);
  resourceDesc.Height = 1;
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.MipLevels = 1;
  resourceDesc.Format = static_cast<DXGI_FORMAT>(DXGI_FORMAT_UNKNOWN);
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.SampleDesc.Quality = 0;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
  if (usage & GPUBufferUsage::READBACK) {
    initialState = D3D12_RESOURCE_STATE_COPY_DEST;
  }

  ComPtr<ID3D12Resource> d3d12Resource = nullptr;
  auto hr = gpu->device()->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                   &resourceDesc, initialState, nullptr,
                                                   IID_PPV_ARGS(&d3d12Resource));
  if (FAILED(hr)) {
    LOGE("D3D12Buffer::Make() CreateCommittedResource failed, HRESULT=0x%08X",
         static_cast<unsigned>(hr));
    return nullptr;
  }

  return gpu->makeResource<D3D12Buffer>(size, usage, std::move(d3d12Resource));
}

D3D12Buffer::D3D12Buffer(size_t size, uint32_t usage, ComPtr<ID3D12Resource> d3d12Resource)
    : GPUBuffer(size, usage), resource(std::move(d3d12Resource)) {
}

void D3D12Buffer::onRelease(D3D12GPU*) {
  if (mappedPointer != nullptr) {
    resource->Unmap(0, nullptr);
    mappedPointer = nullptr;
  }
  resource = nullptr;
}

void* D3D12Buffer::map(size_t offset, size_t size) {
  if (resource == nullptr || mappedPointer != nullptr) {
    return nullptr;
  }
  if (size == 0) {
    LOGE("D3D12Buffer::map() size cannot be 0!");
    return nullptr;
  }
  if (size == GPU_BUFFER_WHOLE_SIZE) {
    size = _size - offset;
  }
  if (offset + size > _size) {
    LOGE("D3D12Buffer::map() range out of bounds!");
    return nullptr;
  }

  D3D12_RANGE readRange = {};
  if (_usage & GPUBufferUsage::READBACK) {
    readRange.Begin = offset;
    readRange.End = offset + size;
  }

  void* data = nullptr;
  auto hr = resource->Map(0, &readRange, &data);
  if (FAILED(hr) || data == nullptr) {
    LOGE("D3D12Buffer::map() Map failed, HRESULT=0x%08X", static_cast<unsigned>(hr));
    return nullptr;
  }

  mappedPointer = static_cast<uint8_t*>(data) + offset;
  return mappedPointer;
}

void D3D12Buffer::unmap() {
  if (resource == nullptr || mappedPointer == nullptr) {
    return;
  }
  D3D12_RANGE writtenRange = {};
  if (!(_usage & GPUBufferUsage::READBACK)) {
    writtenRange.Begin = 0;
    writtenRange.End = _size;
  }
  resource->Unmap(0, &writtenRange);
  mappedPointer = nullptr;
}

bool D3D12Buffer::isReady() const {
  return resource != nullptr;
}

}  // namespace tgfx
