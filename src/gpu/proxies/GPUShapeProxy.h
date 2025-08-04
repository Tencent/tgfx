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

#include "gpu/proxies/TextureProxy.h"
#include "gpu/proxies/VertexBufferProxy.h"

namespace tgfx {
class GPUShapeProxy {
 public:
  GPUShapeProxy(const Matrix& drawingMatrix, std::shared_ptr<VertexBufferProxy> triangles,
                std::shared_ptr<TextureProxy> texture)
      : drawingMatrix(drawingMatrix), triangles(std::move(triangles)), texture(std::move(texture)) {
  }

  Context* getContext() const {
    return triangles ? triangles->getContext() : texture->getContext();
  }

  /**
   * Returns the additional matrix needed to apply to the shape cache when drawing.
   */
  const Matrix& getDrawingMatrix() const {
    return drawingMatrix;
  }

  std::shared_ptr<VertexBuffer> getTriangles() const {
    return triangles ? triangles->getBuffer() : nullptr;
  }

  std::shared_ptr<TextureProxy> getTextureProxy() const {
    return texture;
  }

 private:
  Matrix drawingMatrix = {};
  std::shared_ptr<VertexBufferProxy> triangles = nullptr;
  std::shared_ptr<TextureProxy> texture = nullptr;
};
}  // namespace tgfx
