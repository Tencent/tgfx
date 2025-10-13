/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
class GPUShapeProxy {
 public:
  GPUShapeProxy(const Matrix& drawingMatrix, std::shared_ptr<GPUBufferProxy> triangles,
                std::shared_ptr<TextureProxy> proxy)
      : drawingMatrix(drawingMatrix), triangles(std::move(triangles)),
        textureView(std::move(proxy)) {
  }

  Context* getContext() const {
    return triangles ? triangles->getContext() : textureView->getContext();
  }

  /**
   * Returns the additional matrix needed to apply to the shape cache when drawing.
   */
  const Matrix& getDrawingMatrix() const {
    return drawingMatrix;
  }

  std::shared_ptr<BufferResource> getTriangles() const {
    return triangles ? triangles->getBuffer() : nullptr;
  }

  std::shared_ptr<TextureProxy> getTextureProxy() const {
    return textureView;
  }

 private:
  Matrix drawingMatrix = {};
  std::shared_ptr<GPUBufferProxy> triangles = nullptr;
  std::shared_ptr<TextureProxy> textureView = nullptr;
};
}  // namespace tgfx
