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
    std::shared_ptr<GPUHairlineProxy> hairlineProxy, PMColor color, const Matrix& uvMatrix,
    float coverage, AAType aaType) {
  if (hairlineProxy == nullptr) {
    return nullptr;
  }

  auto allocator = hairlineProxy->getContext()->drawingAllocator();
  return allocator->make<HairlineQuadDrawOp>(allocator, std::move(hairlineProxy), color, uvMatrix,
                                             coverage, aaType);
}

HairlineQuadDrawOp::HairlineQuadDrawOp(BlockAllocator* allocator,
                                       std::shared_ptr<GPUHairlineProxy> hairlineProxy,
                                       PMColor color, const Matrix& uvMatrix, float coverage,
                                       AAType aaType)
    : DrawOp(allocator, aaType), hairlineProxy(std::move(hairlineProxy)), color(color),
      uvMatrix(uvMatrix), coverage(coverage) {
}

PlacementPtr<GeometryProcessor> HairlineQuadDrawOp::onMakeGeometryProcessor(
    RenderTarget* /*renderTarget*/) {
  auto viewMatrix = hairlineProxy->getDrawingMatrix();
  auto realUVMatrix = uvMatrix;
  realUVMatrix.preConcat(viewMatrix);
  return HairlineQuadGeometryProcessor::Make(allocator, color, viewMatrix, realUVMatrix, coverage,
                                             aaType);
}

void HairlineQuadDrawOp::onDraw(RenderPass* renderPass) {
  auto quadVertexBuffer = hairlineProxy->getQuadVertexBufferProxy();
  auto quadIndexBuffer = hairlineProxy->getQuadIndexBufferProxy();

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

  auto gpuVertexBuffer = vertexBuffer->gpuBuffer();
  auto gpuIndexBuffer = indexBuffer->gpuBuffer();
  DEBUG_ASSERT(gpuVertexBuffer != nullptr && gpuIndexBuffer != nullptr);
  renderPass->setVertexBuffer(0, gpuVertexBuffer);
  renderPass->setIndexBuffer(gpuIndexBuffer, IndexFormat::UInt32);
  auto indexCount = static_cast<uint32_t>(indexBuffer->size() / sizeof(uint32_t));
  renderPass->drawIndexed(PrimitiveType::Triangles, 0, indexCount);
}

}  // namespace tgfx
