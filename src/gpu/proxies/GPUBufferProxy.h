/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "ResourceProxy.h"
#include "core/DataSource.h"
#include "gpu/GPUBuffer.h"
#include "tgfx/core/Data.h"

namespace tgfx {
class GPUBufferProxy : public ResourceProxy {
 public:
  /**
   * Creates a GPUBufferProxy from the given data.
   */
  static std::shared_ptr<GPUBufferProxy> MakeFrom(Context* context, std::shared_ptr<Data> data,
                                                  BufferType bufferType, uint32_t renderFlags);
  /**
   * Creates a GPUBufferProxy from the given data provider.
   */
  static std::shared_ptr<GPUBufferProxy> MakeFrom(Context* context,
                                                  std::unique_ptr<DataSource<Data>> source,
                                                  BufferType bufferType, uint32_t renderFlags);

  /**
   * Returns the type of the buffer.
   */
  BufferType bufferType() const {
    return _bufferType;
  }

  /**
   * Returns the associated GPUBuffer instance.
   */
  std::shared_ptr<GPUBuffer> getBuffer() const {
    return std::static_pointer_cast<GPUBuffer>(resource);
  }

 private:
  BufferType _bufferType = BufferType::Vertex;

  explicit GPUBufferProxy(BufferType bufferType);

  friend class ProxyProvider;
};
}  // namespace tgfx
