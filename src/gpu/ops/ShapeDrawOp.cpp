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
std::unique_ptr<ShapeDrawOp> ShapeDrawOp::Make(Color color, std::shared_ptr<Shape> shape,
                                               const Matrix& viewMatrix,
                                               const Rect& deviceClipBounds) {
  if (shape == nullptr) {
    return nullptr;
  }
  auto uvMatrix = Matrix::I();
  if (!viewMatrix.invert(&uvMatrix)) {
    return nullptr;
  }
  shape = Shape::ApplyMatrix(std::move(shape), viewMatrix);
  auto bounds = shape->isInverseFillType() ? deviceClipBounds : shape->getBounds();
  return std::unique_ptr<ShapeDrawOp>(new ShapeDrawOp(color, std::move(shape), uvMatrix, bounds));
}

ShapeDrawOp::ShapeDrawOp(Color color, std::shared_ptr<Shape> s, const Matrix& uvMatrix,
                         const Rect& bounds)
    : DrawOp(ClassID()), color(color), shape(std::move(s)), uvMatrix(uvMatrix) {
  setBounds(bounds);
}

bool ShapeDrawOp::onCombineIfPossible(Op*) {
  // Shapes are rasterized in parallel, so we don't combine them.
  return false;
}

void ShapeDrawOp::prepare(Context* context, uint32_t renderFlags) {
  shapeProxy = context->proxyProvider()->createGpuShapeProxy(shape, aa == AAType::Coverage,
                                                             bounds(), renderFlags);
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
    if (aa == AAType::Coverage) {
      PathTriangulator::ToAATriangles(path, maskRect, &maskVertices);
    } else {
      PathTriangulator::ToTriangles(path, maskRect, &maskVertices);
    }
    vertexData = Data::MakeWithoutCopy(maskVertices.data(), maskVertices.size() * sizeof(float));
  }
  auto pipeline = createPipeline(
      renderPass, DefaultGeometryProcessor::Make(color, renderPass->renderTarget()->width(),
                                                 renderPass->renderTarget()->height(), aa,
                                                 viewMatrix, realUVMatrix));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  auto vertexDataSize = vertexBuffer ? vertexBuffer->size() : vertexData->size();
  auto vertexCount = aa == AAType::Coverage ? PathTriangulator::GetAATriangleCount(vertexDataSize)
                                            : PathTriangulator::GetTriangleCount(vertexDataSize);
  if (vertexBuffer != nullptr) {
    renderPass->bindBuffers(nullptr, vertexBuffer);
  } else {
    renderPass->bindBuffers(nullptr, vertexData);
  }
  renderPass->draw(PrimitiveType::Triangles, 0, vertexCount);
}
}  // namespace tgfx
