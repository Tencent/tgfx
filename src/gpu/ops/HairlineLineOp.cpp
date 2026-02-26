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

#include "HairlineLineOp.h"
#include "gpu/GlobalCache.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

PlacementPtr<HairlineLineOp> HairlineLineOp::Make(std::shared_ptr<GPUHairlineProxy> hairlineProxy,
                                                  PMColor color, const Matrix& uvMatrix,
                                                  float coverage, AAType aaType) {
  if (hairlineProxy == nullptr) {
    return nullptr;
  }

  auto context = hairlineProxy->getContext();
  auto indexBufferProxy = context->globalCache()->getHairlineLineIndexBuffer();
  auto allocator = context->drawingAllocator();
  return allocator->make<HairlineLineOp>(allocator, std::move(hairlineProxy),
                                         std::move(indexBufferProxy), color, uvMatrix, coverage,
                                         aaType);
}

HairlineLineOp::HairlineLineOp(BlockAllocator* allocator,
                               std::shared_ptr<GPUHairlineProxy> hairlineProxy,
                               std::shared_ptr<GPUBufferProxy> indexBufferProxy, PMColor color,
                               const Matrix& uvMatrix, float coverage, AAType aaType)
    : DrawOp(allocator, aaType), hairlineProxy(std::move(hairlineProxy)),
      indexBufferProxy(std::move(indexBufferProxy)), color(color), uvMatrix(uvMatrix),
      coverage(coverage) {
}

PlacementPtr<GeometryProcessor> HairlineLineOp::onMakeGeometryProcessor(
    RenderTarget* /*renderTarget*/) {
  auto viewMatrix = hairlineProxy->getDrawingMatrix();
  auto realUVMatrix = uvMatrix;
  realUVMatrix.preConcat(viewMatrix);
  return HairlineLineGeometryProcessor::Make(allocator, color, viewMatrix, realUVMatrix, coverage,
                                             aaType);
}

void HairlineLineOp::onDraw(RenderPass* renderPass) {
  auto lineVertexBufferProxy = hairlineProxy->getLineVertexBufferProxy();
  if (lineVertexBufferProxy == nullptr || indexBufferProxy == nullptr) {
    return;
  }

  auto vertexBuffer = lineVertexBufferProxy->getBuffer();
  if (vertexBuffer == nullptr) {
    return;
  }

  auto gpuVertexBuffer = vertexBuffer->gpuBuffer();
  if (gpuVertexBuffer == nullptr) {
    return;
  }

  auto indexBuffer = indexBufferProxy->getBuffer();
  if (indexBuffer == nullptr) {
    return;
  }

  auto gpuIndexBuffer = indexBuffer->gpuBuffer();
  if (gpuIndexBuffer == nullptr) {
    return;
  }

  auto totalLineCount = vertexBuffer->size() / (VerticesPerLine * BytesPerLineVertex);
  size_t vertexOffset = 0;
  renderPass->setIndexBuffer(gpuIndexBuffer, IndexFormat::UInt32);

  while (totalLineCount > 0) {
    auto batchLineCount = std::min(totalLineCount, MAX_NUM_HAIRLINE_LINES);
    auto indexCount = static_cast<uint32_t>(batchLineCount * IndicesPerLine);
    renderPass->setVertexBuffer(0, gpuVertexBuffer, vertexOffset);
    renderPass->drawIndexed(PrimitiveType::Triangles, indexCount);
    totalLineCount -= batchLineCount;
    vertexOffset += batchLineCount * VerticesPerLine * BytesPerLineVertex;
  }
}

}  // namespace tgfx
