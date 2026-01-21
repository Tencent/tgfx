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

#include "GPUMeshProxy.h"

namespace tgfx {

GPUMeshProxy::GPUMeshProxy(Context* context, std::shared_ptr<Mesh> mesh)
    : _context(context), _mesh(std::move(mesh)) {
}

Rect GPUMeshProxy::bounds() const {
  return _mesh ? _mesh->bounds() : Rect::MakeEmpty();
}

static std::shared_ptr<GPUBuffer> GetGPUBuffer(const std::shared_ptr<GPUBufferProxy>& proxy) {
  if (proxy == nullptr) {
    return nullptr;
  }
  auto bufferResource = proxy->getBuffer();
  if (bufferResource == nullptr) {
    return nullptr;
  }
  return bufferResource->gpuBuffer();
}

std::shared_ptr<GPUBuffer> GPUMeshProxy::getVertexBuffer() const {
  return GetGPUBuffer(vertexBufferProxy);
}

std::shared_ptr<GPUBuffer> GPUMeshProxy::getIndexBuffer() const {
  return GetGPUBuffer(indexBufferProxy);
}

}  // namespace tgfx
