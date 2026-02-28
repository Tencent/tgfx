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
 * GPUHairlineProxy contains vertex buffer proxies required for hairline rendering.
 */
class GPUHairlineProxy {
 public:
  GPUHairlineProxy(const Matrix& drawingMatrix,
                   std::shared_ptr<GPUBufferProxy> lineVertexBuffer,
                   std::shared_ptr<GPUBufferProxy> quadVertexBuffer)
      : drawingMatrix(drawingMatrix),
        lineVertexProxy(std::move(lineVertexBuffer)),
        quadVertexProxy(std::move(quadVertexBuffer)) {
  }

  std::shared_ptr<GPUBufferProxy> getLineVertexBufferProxy() const {
    return lineVertexProxy;
  }

  std::shared_ptr<GPUBufferProxy> getQuadVertexBufferProxy() const {
    return quadVertexProxy;
  }

  const Matrix& getDrawingMatrix() const {
    return drawingMatrix;
  }

  Context* getContext() const {
    if (lineVertexProxy) return lineVertexProxy->context;
    if (quadVertexProxy) return quadVertexProxy->context;
    return nullptr;
  }

 private:
  Matrix drawingMatrix;
  std::shared_ptr<GPUBufferProxy> lineVertexProxy;
  std::shared_ptr<GPUBufferProxy> quadVertexProxy;
};

}  // namespace tgfx
