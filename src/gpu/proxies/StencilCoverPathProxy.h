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

#pragma once

#include "gpu/proxies/GPUBufferProxy.h"

namespace tgfx {
/**
 * StencilCoverPathProxy holds the GPU vertex buffer produced by the stencil-and-cover
 * render path. The vertex stream stores per-vertex position together with the
 * implicit-curve coefficients consumed by the stencil pass; transforms and per-draw color are
 * supplied through uniforms by the draw op so multiple draws of the same shape can share the
 * same cached geometry.
 */
class StencilCoverPathProxy {
 public:
  explicit StencilCoverPathProxy(std::shared_ptr<GPUBufferProxy> vertexBuffer)
      : vertexBuffer(std::move(vertexBuffer)) {
  }

  Context* getContext() const {
    return vertexBuffer ? vertexBuffer->getContext() : nullptr;
  }

  /**
   * Returns the underlying vertex buffer proxy. Two cache-hit calls to the proxy provider with
   * the same shape return geometry proxies that share the same vertex buffer proxy instance.
   */
  std::shared_ptr<GPUBufferProxy> getVertexBufferProxy() const {
    return vertexBuffer;
  }

  /**
   * Returns the underlying vertex buffer resource. May be nullptr while the upload task is
   * still pending or when the rasterizer produced no drawable geometry.
   */
  std::shared_ptr<BufferResource> getVertexBuffer() const {
    return vertexBuffer ? vertexBuffer->getBuffer() : nullptr;
  }

 private:
  std::shared_ptr<GPUBufferProxy> vertexBuffer = nullptr;
};
}  // namespace tgfx
