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

#include "ShapeDrawOp.h"
#include "core/PathTriangulator.h"
#include "core/utils/Log.h"
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
PlacementPtr<ShapeDrawOp> ShapeDrawOp::Make(std::shared_ptr<GpuShapeProxy> shapeProxy, Color color,
                                            const Matrix& uvMatrix, AAType aaType) {
  if (shapeProxy == nullptr) {
    return nullptr;
  }
  auto drawingBuffer = shapeProxy->getContext()->drawingBuffer();
  return drawingBuffer->make<ShapeDrawOp>(std::move(shapeProxy), color, uvMatrix, aaType);
}

ShapeDrawOp::ShapeDrawOp(std::shared_ptr<GpuShapeProxy> shapeProxy, Color color,
                         const Matrix& uvMatrix, AAType aaType)
    : DrawOp(aaType), shapeProxy(std::move(shapeProxy)), color(color), uvMatrix(uvMatrix) {
}

void ShapeDrawOp::execute(RenderPass* renderPass) {
  if (shapeProxy == nullptr) {
    return;
  }
  auto viewMatrix = shapeProxy->getDrawingMatrix();
  auto realUVMatrix = uvMatrix;
  realUVMatrix.preConcat(viewMatrix);
  auto vertexBuffer = shapeProxy->getTriangles();
  std::shared_ptr<Data> vertexData = nullptr;
  if (vertexBuffer == nullptr) {
    auto textureProxy = shapeProxy->getTextureProxy();
    if (textureProxy == nullptr) {
      return;
    }
    Matrix maskMatrix = Matrix::I();
    if (!realUVMatrix.invert(&maskMatrix)) {
      return;
    }
    auto maskRect = Rect::MakeWH(textureProxy->width(), textureProxy->height());
    auto maskFP = TextureEffect::Make(std::move(textureProxy), {}, &maskMatrix, true);
    if (maskFP == nullptr) {
      return;
    }
    addCoverageFP(std::move(maskFP));
    Path path = {};
    path.addRect(maskRect);
    if (aaType == AAType::Coverage) {
      PathTriangulator::ToAATriangles(path, maskRect, &maskVertices);
    } else {
      PathTriangulator::ToTriangles(path, maskRect, &maskVertices);
    }
    vertexData = Data::MakeWithoutCopy(maskVertices.data(), maskVertices.size() * sizeof(float));
  }
  auto drawingBuffer = renderPass->getContext()->drawingBuffer();
  auto renderTarget = renderPass->renderTarget();
  auto gp =
      DefaultGeometryProcessor::Make(drawingBuffer, color, renderTarget->width(),
                                     renderTarget->height(), aaType, viewMatrix, realUVMatrix);
  auto pipeline = createPipeline(renderPass, std::move(gp));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  auto vertexDataSize = vertexBuffer ? vertexBuffer->size() : vertexData->size();
  auto vertexCount = aaType == AAType::Coverage
                         ? PathTriangulator::GetAATriangleCount(vertexDataSize)
                         : PathTriangulator::GetTriangleCount(vertexDataSize);
  if (vertexBuffer != nullptr) {
    renderPass->bindBuffers(nullptr, vertexBuffer);
  } else {
    renderPass->bindBuffers(nullptr, vertexData);
  }
  renderPass->draw(PrimitiveType::Triangles, 0, vertexCount);
}
}  // namespace tgfx
