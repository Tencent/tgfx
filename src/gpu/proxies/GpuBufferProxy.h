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

#include "ResourceProxy.h"
#include "core/DataProvider.h"
#include "gpu/GpuBuffer.h"

namespace tgfx {
class GpuBufferProxy : public ResourceProxy {
 public:
  /**
   * Creates a GpuBufferProxy from the given data.
   */
  static std::shared_ptr<GpuBufferProxy> MakeFrom(Context* context, std::shared_ptr<Data> data,
                                                  BufferType bufferType);
  /**
   * Creates a GpuBufferProxy from the given data provider.
   */
  static std::shared_ptr<GpuBufferProxy> MakeFrom(Context* context,
                                                  std::shared_ptr<DataProvider> dataProvider,
                                                  BufferType bufferType);

  /**
   * Returns the type of the buffer.
   */
  BufferType bufferType() const {
    return _bufferType;
  }

  /**
   * Returns the associated GpuBuffer instance.
   */
  std::shared_ptr<GpuBuffer> getBuffer() const;

 private:
  BufferType _bufferType = BufferType::Vertex;
  size_t _size = 0;

  explicit GpuBufferProxy(BufferType bufferType) : _bufferType(bufferType) {
  }

  friend class ProxyProvider;
};
}  // namespace tgfx
