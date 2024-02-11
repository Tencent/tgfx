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

#include "TriangulatingShape.h"
#include "gpu/GpuBuffer.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/TriangulatingPathOp.h"

namespace tgfx {
class TriangleVerticesProvider : public DataProvider {
 public:
  TriangleVerticesProvider(Path path, const Rect& clipBounds)
      : path(std::move(path)), clipBounds(clipBounds) {
  }

  std::shared_ptr<Data> getData() const override {
    std::vector<float> vertices = {};
    auto count = PathTriangulator::ToAATriangles(path, clipBounds, &vertices);
    if (count == 0) {
      // The path is not a filled path, or it is invisible.
      return nullptr;
    }
    return Data::MakeWithCopy(vertices.data(), vertices.size() * sizeof(float));
  }

 private:
  Path path = {};
  Rect clipBounds = Rect::MakeEmpty();
};

TriangulatingShape::TriangulatingShape(std::shared_ptr<PathProxy> pathProxy, float resolutionScale)
    : PathShape(std::move(pathProxy), resolutionScale) {
  auto path = getFillPath();
  resourceKey = ResourceKey::Make();
  triangulator = std::make_shared<TriangleVerticesProvider>(path, bounds);
}

std::unique_ptr<DrawOp> TriangulatingShape::makeOp(Context* context, const Color& color,
                                                   const Matrix& viewMatrix,
                                                   uint32_t renderFlags) const {
  auto proxyProvider = context->proxyProvider();
  auto bufferProxy = proxyProvider->createGpuBufferProxy(resourceKey, triangulator,
                                                         BufferType::Vertex, renderFlags);
  if (bufferProxy == nullptr) {
    return nullptr;
  }
  return std::make_unique<TriangulatingPathOp>(color, std::move(bufferProxy), bounds, viewMatrix);
}
}  // namespace tgfx
