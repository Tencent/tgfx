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
    std::shared_ptr<GPUHairlineProxy> hairlineProxy, PMColor color, const Matrix& uvMatrix,
    float coverage, AAType aaType) {
  if (hairlineProxy == nullptr) {
    return nullptr;
  }

  auto allocator = hairlineProxy->getContext()->drawingAllocator();
  return allocator->make<HairlineLineDrawOp>(allocator, std::move(hairlineProxy), color, uvMatrix,
                                             coverage, aaType);
}

HairlineLineDrawOp::HairlineLineDrawOp(BlockAllocator* allocator,
                                       std::shared_ptr<GPUHairlineProxy> hairlineProxy,
                                       PMColor color, const Matrix& uvMatrix, float coverage,
                                       AAType aaType)
    : DrawOp(allocator, aaType), hairlineProxy(std::move(hairlineProxy)), color(color),
      uvMatrix(uvMatrix), coverage(coverage) {
}

PlacementPtr<GeometryProcessor> HairlineLineDrawOp::onMakeGeometryProcessor(
    RenderTarget* /*renderTarget*/) {
  auto viewMatrix = hairlineProxy->getDrawingMatrix();
  auto realUVMatrix = uvMatrix;
  realUVMatrix.preConcat(viewMatrix);
  return HairlineLineGeometryProcessor::Make(allocator, color, viewMatrix, realUVMatrix, coverage,
                                             aaType);
}

void HairlineLineDrawOp::onDraw(RenderPass* renderPass) {
  auto lineVertexBuffer = hairlineProxy->getLineVertexBufferProxy();
  auto lineIndexBuffer = hairlineProxy->getLineIndexBufferProxy();

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
