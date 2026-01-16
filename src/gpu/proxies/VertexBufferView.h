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

#include "gpu/proxies/GPUBufferProxy.h"

namespace tgfx {
/**
 * VertexBufferView is a view of a GPUBufferProxy that allows access to a specific range of
 * the vertex buffer.
 */
class VertexBufferView {
 public:
  VertexBufferView(std::shared_ptr<GPUBufferProxy> proxy, size_t _offset, size_t _size)
      : proxy(std::move(proxy)), _offset(_offset), _size(_size) {
  }

  /**
   * Returns the BufferResource associated with this VertexBufferView.
   */
  std::shared_ptr<BufferResource> getBuffer() const {
    return proxy ? proxy->getBuffer() : nullptr;
  }

  /**
   * Returns the offset of the vertex data in the vertex buffer.
   */
  size_t offset() const {
    return _offset;
  }

  /**
   * Returns the size of the vertex data in the vertex buffer.
   */
  size_t size() const {
    return _size;
  }

 private:
  std::shared_ptr<GPUBufferProxy> proxy = nullptr;
  size_t _offset = 0;
  size_t _size = 0;
};
}  // namespace tgfx
