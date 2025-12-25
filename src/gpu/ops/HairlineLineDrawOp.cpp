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

#include "HairlineLineDrawOp.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

PlacementPtr<HairlineLineDrawOp> HairlineLineDrawOp::Make(
    std::shared_ptr<GPUBufferProxy> lineVertexBuffer,
    std::shared_ptr<GPUBufferProxy> lineIndexBuffer, PMColor color, const Matrix& uvMatrix) {
  if (lineVertexBuffer == nullptr || lineIndexBuffer == nullptr) {
    return nullptr;
  }

  auto allocator = lineVertexBuffer->getContext()->drawingAllocator();
  return allocator->make<HairlineLineDrawOp>(allocator, std::move(lineVertexBuffer),
                                             std::move(lineIndexBuffer), color, uvMatrix);
}

HairlineLineDrawOp::HairlineLineDrawOp(BlockAllocator* allocator,
                                       std::shared_ptr<GPUBufferProxy> vertexBuffer,
                                       std::shared_ptr<GPUBufferProxy> indexBuffer, PMColor color,
                                       const Matrix& uvMatrix)
    : DrawOp(allocator, AAType::Coverage), lineVertexBuffer(std::move(vertexBuffer)),
      lineIndexBuffer(std::move(indexBuffer)), color(color), uvMatrix(uvMatrix) {
}

PlacementPtr<GeometryProcessor> HairlineLineDrawOp::onMakeGeometryProcessor(
    RenderTarget* /*renderTarget*/) {
  return HairlineLineGeometryProcessor::Make(allocator, color, Matrix::I(), uvMatrix,
                                             static_cast<uint8_t>(0xFF));
}

void HairlineLineDrawOp::onDraw(RenderPass* renderPass) {
  if (lineVertexBuffer == nullptr || lineIndexBuffer == nullptr) {
    return;
  }

  auto vertexBuffer = lineVertexBuffer->getBuffer();
  if (vertexBuffer == nullptr) {
    return;
  }

  auto indexBuffer = lineIndexBuffer->getBuffer();
  if (indexBuffer == nullptr) {
    return;
  }

  renderPass->setVertexBuffer(vertexBuffer->gpuBuffer());
  renderPass->setIndexBuffer(indexBuffer->gpuBuffer(), IndexFormat::UInt32);
  renderPass->drawIndexed(PrimitiveType::Triangles, 0, indexBuffer->size() / sizeof(uint32_t));
}

}  // namespace tgfx
