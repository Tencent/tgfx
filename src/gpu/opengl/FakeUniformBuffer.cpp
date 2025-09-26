/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "FakeUniformBuffer.h"
#include "core/utils/Log.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

FakeUniformBuffer::FakeUniformBuffer(size_t size)
    : GPUBuffer(size, GPUBufferUsage::UNIFORM), uniqueID(UniqueID::Next()) {
  _data = malloc(size);
}

FakeUniformBuffer::~FakeUniformBuffer() {
  releaseInternal();
}

void* FakeUniformBuffer::map(GPU*, size_t offset, size_t size) {
  if (offset + size > _size) {
    LOGE("FakeUniformBuffer::map() out of range!");
    return nullptr;
  }

  isMapped = true;
  _mappedOffset = offset;
  _mappedSize = size;
  mappedRange = static_cast<uint8_t*>(_data) + offset;

  return mappedRange;
}

void FakeUniformBuffer::unmap(GPU*) {
  if (!isMapped) {
    return;
  }
  isMapped = false;
  mappedRange = nullptr;
}

void FakeUniformBuffer::releaseInternal() {
  if (_data != nullptr) {
    free(_data);
  }
  mappedRange = nullptr;
  isMapped = false;
}
}  // namespace tgfx
