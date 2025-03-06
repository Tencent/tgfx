/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "gpu/Resource.h"

namespace tgfx {
enum class BufferType {
  Index,
  Vertex,
};

class GpuBuffer : public Resource {
 public:
  static std::shared_ptr<GpuBuffer> Make(Context* context, BufferType bufferType,
                                         const void* buffer = nullptr, size_t size = 0);

  BufferType bufferType() const {
    return _bufferType;
  }

  size_t size() const {
    return _size;
  }

  size_t memoryUsage() const override {
    return _size;
  }

 protected:
  BufferType _bufferType;
  size_t _size;

  GpuBuffer(BufferType bufferType, size_t sizeInBytes)
      : _bufferType(bufferType), _size(sizeInBytes) {
  }
};
}  // namespace tgfx
