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

#include "HairlineQuadDrawOp.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

PlacementPtr<HairlineQuadDrawOp> HairlineQuadDrawOp::Make(
    std::shared_ptr<GPUBufferProxy> quadVertexBuffer,
    std::shared_ptr<GPUBufferProxy> quadIndexBuffer, PMColor color, const Matrix& uvMatrix) {
  if (quadVertexBuffer == nullptr || quadIndexBuffer == nullptr) {
    return nullptr;
  }

  auto allocator = quadVertexBuffer->getContext()->drawingAllocator();
  return allocator->make<HairlineQuadDrawOp>(allocator, std::move(quadVertexBuffer),
                                             std::move(quadIndexBuffer), color, uvMatrix);
}

HairlineQuadDrawOp::HairlineQuadDrawOp(BlockAllocator* allocator,
                                       std::shared_ptr<GPUBufferProxy> vertexBuffer,
                                       std::shared_ptr<GPUBufferProxy> indexBuffer, PMColor color,
                                       const Matrix& uvMatrix)
    : DrawOp(allocator, AAType::Coverage), quadVertexBuffer(std::move(vertexBuffer)),
      quadIndexBuffer(std::move(indexBuffer)), color(color), uvMatrix(uvMatrix) {
}

PlacementPtr<GeometryProcessor> HairlineQuadDrawOp::onMakeGeometryProcessor(
    RenderTarget* renderTarget [[maybe_unused]]) {
  return HairlineQuadGeometryProcessor::Make(allocator, color, Matrix::I(), uvMatrix,
                                             static_cast<uint8_t>(0xff));
}

void HairlineQuadDrawOp::onDraw(RenderPass* renderPass) {
  if (quadVertexBuffer == nullptr || quadIndexBuffer == nullptr) {
    return;
  }

  auto vertexBuffer = quadVertexBuffer->getBuffer();
  if (vertexBuffer == nullptr) {
    return;
  }

  auto indexBuffer = quadIndexBuffer->getBuffer();
  if (indexBuffer == nullptr) {
    return;
  }

  renderPass->setVertexBuffer(vertexBuffer->gpuBuffer());
  renderPass->setIndexBuffer(indexBuffer->gpuBuffer(), IndexFormat::UInt32);
  renderPass->drawIndexed(PrimitiveType::Triangles, 0, indexBuffer->size() / sizeof(uint32_t));
}

}  // namespace tgfx
