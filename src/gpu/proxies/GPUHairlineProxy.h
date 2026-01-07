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
#include "tgfx/core/Matrix.h"

namespace tgfx {

/**
 * GPUHairlineProxy 包含 hairline 渲染所需的顶点和索引缓冲 proxy
 */
class GPUHairlineProxy {
 public:
  GPUHairlineProxy(const Matrix& drawingMatrix,
                   std::shared_ptr<GPUBufferProxy> lineVertexBuffer,
                   std::shared_ptr<GPUBufferProxy> lineIndexBuffer,
                   std::shared_ptr<GPUBufferProxy> quadVertexBuffer,
                   std::shared_ptr<GPUBufferProxy> quadIndexBuffer)
      : drawingMatrix(drawingMatrix),
        lineVertexProxy(std::move(lineVertexBuffer)),
        lineIndexProxy(std::move(lineIndexBuffer)),
        quadVertexProxy(std::move(quadVertexBuffer)),
        quadIndexProxy(std::move(quadIndexBuffer)) {
  }

  std::shared_ptr<GPUBufferProxy> getLineVertexBufferProxy() const {
    return lineVertexProxy;
  }

  std::shared_ptr<GPUBufferProxy> getLineIndexBufferProxy() const {
    return lineIndexProxy;
  }

  std::shared_ptr<GPUBufferProxy> getQuadVertexBufferProxy() const {
    return quadVertexProxy;
  }

  std::shared_ptr<GPUBufferProxy> getQuadIndexBufferProxy() const {
    return quadIndexProxy;
  }

  const Matrix& getDrawingMatrix() const {
    return drawingMatrix;
  }

  Context* getContext() const {
    return lineVertexProxy ? lineVertexProxy->context : nullptr;
  }

 private:
  Matrix drawingMatrix;
  std::shared_ptr<GPUBufferProxy> lineVertexProxy;
  std::shared_ptr<GPUBufferProxy> lineIndexProxy;
  std::shared_ptr<GPUBufferProxy> quadVertexProxy;
  std::shared_ptr<GPUBufferProxy> quadIndexProxy;
};

}  // namespace tgfx
