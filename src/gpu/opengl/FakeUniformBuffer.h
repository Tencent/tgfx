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

#pragma once
#include "gpu/GPUBuffer.h"

namespace tgfx {
static constexpr size_t MAX_FAKE_UNIFORM_BUFFER_SIZE = 64 * 1024;

class FakeUniformBuffer : public GPUBuffer
{
public:
  explicit FakeUniformBuffer(size_t size);

  ~FakeUniformBuffer() override;

  void* map(GPU*, size_t offset, size_t size) override;

  void unmap(GPU*) override;

  const void* data() const {
    return _data;
  }

private:
  uint32_t uniqueID = 0;
  size_t _mappedOffset = 0;
  size_t _mappedSize = 0;
  void* _data = nullptr;

  void releaseInternal();
};
} // namespace tgfx
