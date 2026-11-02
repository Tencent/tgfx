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

#include "HairlineQuadOp.h"
#include "gpu/GlobalCache.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

PlacementPtr<HairlineQuadOp> HairlineQuadOp::Make(std::shared_ptr<GPUHairlineProxy> hairlineProxy,
                                                  PMColor color, const Matrix& uvMatrix,
                                                  float coverage, AAType aaType) {
  if (hairlineProxy == nullptr) {
    return nullptr;
  }

  auto context = hairlineProxy->getContext();
  auto indexBufferProxy = context->globalCache()->getHairlineQuadIndexBuffer();
  auto allocator = context->drawingAllocator();
  return allocator->make<HairlineQuadOp>(allocator, std::move(hairlineProxy),
                                         std::move(indexBufferProxy), color, uvMatrix, coverage,
                                         aaType);
}

HairlineQuadOp::HairlineQuadOp(BlockAllocator* allocator,
                               std::shared_ptr<GPUHairlineProxy> hairlineProxy,
                               std::shared_ptr<GPUBufferProxy> indexBufferProxy, PMColor color,
                               const Matrix& uvMatrix, float coverage, AAType aaType)
    : StandardDrawOp(allocator, aaType), hairlineProxy(std::move(hairlineProxy)),
      indexBufferProxy(std::move(indexBufferProxy)), color(color), uvMatrix(uvMatrix),
      coverage(coverage) {
}

PlacementPtr<GeometryProcessor> HairlineQuadOp::onMakeGeometryProcessor(
    RenderTarget* /*renderTarget*/) {
  auto viewMatrix = hairlineProxy->getDrawingMatrix();
  auto realUVMatrix = uvMatrix;
  realUVMatrix.preConcat(viewMatrix);
  return HairlineQuadGeometryProcessor::Make(allocator, color, viewMatrix, realUVMatrix, coverage,
                                             aaType);
}

bool HairlineQuadOp::onPrepare() {
  // The hairline vertex buffer is produced asynchronously and may be empty when the path has
  // no quadratic segments after decomposition. The index buffer comes from the global cache
  // and is also uploaded asynchronously. Skip the op entirely if either resource is unavailable
  // so the pipeline is not bound and no dirty state is left for the next op.
  auto quadVertexBufferProxy = hairlineProxy->getQuadVertexBufferProxy();
  if (quadVertexBufferProxy == nullptr || quadVertexBufferProxy->getBuffer() == nullptr) {
    return false;
  }
  if (indexBufferProxy == nullptr || indexBufferProxy->getBuffer() == nullptr) {
    return false;
  }
  return true;
}

void HairlineQuadOp::onDraw(RenderPass* renderPass, RenderTarget* /*renderTarget*/) {
  auto indexBuffer = indexBufferProxy->getBuffer();
  auto vertexBuffer = hairlineProxy->getQuadVertexBufferProxy()->getBuffer();
  auto totalQuadCount = vertexBuffer->size() / (VerticesPerQuad * BytesPerQuadVertex);
  size_t vertexOffset = 0;
  renderPass->setIndexBuffer(indexBuffer->gpuBuffer(), IndexFormat::UInt16);

  while (totalQuadCount > 0) {
    auto batchQuadCount = std::min(totalQuadCount, MaxNumQuads);
    auto indexCount = static_cast<uint32_t>(batchQuadCount * IndicesPerQuad);
    renderPass->setVertexBuffer(0, vertexBuffer->gpuBuffer(), vertexOffset);
    renderPass->drawIndexed(PrimitiveType::Triangles, indexCount);
    totalQuadCount -= batchQuadCount;
    vertexOffset += batchQuadCount * VerticesPerQuad * BytesPerQuadVertex;
  }
}

}  // namespace tgfx
