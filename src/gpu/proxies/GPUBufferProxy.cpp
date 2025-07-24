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

#include "GPUBufferProxy.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<GPUBufferProxy> GPUBufferProxy::MakeFrom(Context* context,
                                                         std::shared_ptr<Data> data,
                                                         BufferType bufferType,
                                                         uint32_t renderFlags) {
  if (context == nullptr) {
    return nullptr;
  }
  return context->proxyProvider()->createGPUBufferProxy({}, std::move(data), bufferType,
                                                        renderFlags);
}

std::shared_ptr<GPUBufferProxy> GPUBufferProxy::MakeFrom(Context* context,
                                                         std::unique_ptr<DataSource<Data>> source,
                                                         BufferType bufferType,
                                                         uint32_t renderFlags) {
  if (context == nullptr) {
    return nullptr;
  }
  return context->proxyProvider()->createGPUBufferProxy({}, std::move(source), bufferType,
                                                        renderFlags);
}

GPUBufferProxy::GPUBufferProxy(BufferType bufferType) : _bufferType(bufferType) {
}
}  // namespace tgfx