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
TriangulatingShape::TriangulatingShape(std::shared_ptr<PathProxy> pathProxy, float resolutionScale)
    : PathShape(std::move(pathProxy), resolutionScale) {
  auto path = getFillPath();
  triangulator = std::make_shared<PathTriangulator>(path, bounds);
}

std::unique_ptr<DrawOp> TriangulatingShape::makeOp(GpuPaint* paint, const Matrix& viewMatrix,
                                                   uint32_t renderFlags) const {
  auto proxyProvider = paint->context->proxyProvider();
  auto bufferProxy =
      proxyProvider->createGpuBufferProxy(uniqueKey, triangulator, BufferType::Vertex, renderFlags);
  if (bufferProxy == nullptr) {
    return nullptr;
  }
  return std::make_unique<TriangulatingPathOp>(paint->color, bufferProxy, bounds, viewMatrix);
}
}  // namespace tgfx
