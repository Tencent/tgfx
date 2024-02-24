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

#include "TriangulatingPathOp.h"
#include "core/PathTriangulator.h"
#include "gpu/Gpu.h"
#include "gpu/processors/DefaultGeometryProcessor.h"

namespace tgfx {
std::unique_ptr<TriangulatingPathOp> TriangulatingPathOp::Make(
    Color color, std::shared_ptr<GpuBufferProxy> vertexBuffer, const Rect& bounds,
    const Matrix& viewMatrix) {
  if (vertexBuffer == nullptr || bounds.isEmpty()) {
    return nullptr;
  }
  return std::unique_ptr<TriangulatingPathOp>(
      new TriangulatingPathOp(color, std::move(vertexBuffer), bounds, viewMatrix));
}

TriangulatingPathOp::TriangulatingPathOp(Color color, std::shared_ptr<GpuBufferProxy> vertexBuffer,
                                         const Rect& bounds, const Matrix& viewMatrix)
    : DrawOp(ClassID()), color(color), vertexBuffer(std::move(vertexBuffer)),
      viewMatrix(viewMatrix) {
  setBounds(bounds);
}

bool TriangulatingPathOp::onCombineIfPossible(Op*) {
  return false;
}

void TriangulatingPathOp::execute(RenderPass* renderPass) {
  auto buffer = vertexBuffer->getBuffer();
  if (buffer == nullptr) {
    return;
  }
  auto pipeline = createPipeline(
      renderPass, DefaultGeometryProcessor::Make(color, renderPass->renderTarget()->width(),
                                                 renderPass->renderTarget()->height(), viewMatrix,
                                                 Matrix::I()));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(nullptr, buffer);
  auto vertexCount = PathTriangulator::GetAATriangleCount(buffer->size());
  renderPass->draw(PrimitiveType::Triangles, 0, vertexCount);
}
}  // namespace tgfx
