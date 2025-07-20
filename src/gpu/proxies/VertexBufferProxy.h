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

#include "core/utils/Log.h"
#include "gpu/proxies/GpuBufferProxy.h"

namespace tgfx {
/**
 * VertexBufferProxy serves as a proxy for a vertex buffer, referencing a specific range within a
 * GpuBuffer through GpuBufferProxy. It keeps track of the offset and size of the data in the buffer.
 */
class VertexBufferProxy {
 public:
  VertexBufferProxy(std::shared_ptr<GpuBufferProxy> bufferProxy, size_t offset)
      : proxy(std::move(bufferProxy)), _offset(offset) {
    DEBUG_ASSERT(proxy != nullptr);
  }

  /**
   * Returns the GpuBuffer associated with this VertexBufferProxy.
   */
  std::shared_ptr<GpuBuffer> getBuffer() const {
    return proxy ? proxy->getBuffer() : nullptr;
  }

  /**
   * Returns the offset of the vertex data in the buffer.
   */
  size_t offset() const {
    return _offset;
  }

  /**
   * Returns the size of the vertex data in the buffer.
   */
  size_t size() const {
    return _size;
  }

 private:
  std::shared_ptr<GpuBufferProxy> proxy = nullptr;
  size_t _offset = 0;
  size_t _size = 0;
};
}  // namespace tgfx
