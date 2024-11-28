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
#include "core/shapes/MatrixShape.h"
#include "core/utils/Log.h"
#include "core/utils/Profiling.h"
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
std::unique_ptr<ShapeDrawOp> ShapeDrawOp::Make(Color color, std::shared_ptr<Shape> shape,
                                               const Matrix& viewMatrix) {
  TRACE_EVENT;
  if (shape == nullptr) {
    return nullptr;
  }
  auto uvMatrix = Matrix::I();
  auto totalMatrix = viewMatrix;
  if (shape->type() == Shape::Type::Matrix) {
    // always unwrap the matrix shape.
    auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
    uvMatrix = matrixShape->matrix;
    totalMatrix.preConcat(matrixShape->matrix);
    shape = matrixShape->shape;
  }
  return std::unique_ptr<ShapeDrawOp>(
      new ShapeDrawOp(color, std::move(shape), totalMatrix, uvMatrix));
}

ShapeDrawOp::ShapeDrawOp(Color color, std::shared_ptr<Shape> s, const Matrix& viewMatrix,
                         const Matrix& uvMatrix)
    : DrawOp(ClassID()), color(color), shape(std::move(s)), viewMatrix(viewMatrix),
      uvMatrix(uvMatrix) {
  auto bounds = shape->getBounds(viewMatrix.getMaxScale());
  viewMatrix.mapRect(&bounds);
  setBounds(bounds);
}

bool ShapeDrawOp::onCombineIfPossible(Op*) {
  // Shapes are rasterized in parallel, so we don't combine them.
  return false;
}

void ShapeDrawOp::prepare(Context* context, uint32_t renderFlags) {
  TRACE_EVENT;
  auto matrix = viewMatrix;
  auto scales = viewMatrix.getAxisScales();
  if (scales.x == scales.y) {
    DEBUG_ASSERT(scales.x != 0.0f);
    matrix.setScale(scales.x, scales.y);
  }
  auto rasterizeShape = Shape::ApplyMatrix(shape, matrix);
  shapeProxy = context->proxyProvider()->createGpuShapeProxy(std::move(rasterizeShape),
                                                             aa == AAType::Coverage, renderFlags);
  if (shapeProxy) {
    auto& bounds = shapeProxy->getBounds();
    matrix.postTranslate(-bounds.x(), -bounds.y());
  }
  rasterizeMatrix = matrix;
}

static std::shared_ptr<Data> MakeVertexData(const Rect& rect) {
  Buffer buffer(8 * sizeof(float));
  auto vertices = static_cast<float*>(buffer.data());
  auto quad = Quad::MakeFrom(rect);
  int index = 0;
  for (size_t i = 4; i >= 1; --i) {
    vertices[index++] = quad.point(i - 1).x;
    vertices[index++] = quad.point(i - 1).y;
  }
  return buffer.release();
}

static std::shared_ptr<Data> MakeAAVertexData(const Rect& rect) {
  Buffer buffer(24 * sizeof(float));
  auto vertices = static_cast<float*>(buffer.data());
  // There is no need to scale the padding by the view matrix scale, as the view matrix scale is
  // already applied to the path.
  auto padding = 0.5f;
  auto insetBounds = rect.makeInset(padding, padding);
  auto insetQuad = Quad::MakeFrom(insetBounds);
  auto outsetBounds = rect.makeOutset(padding, padding);
  auto outsetQuad = Quad::MakeFrom(outsetBounds);
  auto index = 0;
  for (int j = 0; j < 2; ++j) {
    const auto& quad = j == 0 ? insetQuad : outsetQuad;
    auto coverage = j == 0 ? 1.0f : 0.0f;
    for (size_t k = 0; k < 4; ++k) {
      vertices[index++] = quad.point(k).x;
      vertices[index++] = quad.point(k).y;
      vertices[index++] = coverage;
    }
  }
  return buffer.release();
}

void ShapeDrawOp::execute(RenderPass* renderPass) {
  TRACE_EVENT;
  if (shapeProxy == nullptr) {
    return;
  }
  auto invert = Matrix::I();
  if (!rasterizeMatrix.invert(&invert)) {
    return;
  }
  auto realUVMatrix = uvMatrix;
  realUVMatrix.preConcat(invert);
  auto realViewMatrix = viewMatrix;
  realViewMatrix.preConcat(invert);
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
    auto rect = Rect::MakeWH(textureProxy->width(), textureProxy->height());
    vertexData = aa == AAType::Coverage ? MakeAAVertexData(rect) : MakeVertexData(rect);
    auto maskFP = TextureEffect::Make(std::move(textureProxy), {}, &maskMatrix, true);
    if (maskFP == nullptr) {
      return;
    }
    addCoverageFP(std::move(maskFP));
  }
  auto pipeline = createPipeline(
      renderPass, DefaultGeometryProcessor::Make(color, renderPass->renderTarget()->width(),
                                                 renderPass->renderTarget()->height(), aa,
                                                 realViewMatrix, realUVMatrix));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  auto vertexDataSize = vertexBuffer ? vertexBuffer->size() : vertexData->size();
  auto vertexCount = aa == AAType::Coverage ? PathTriangulator::GetAATriangleCount(vertexDataSize)
                                            : PathTriangulator::GetTriangleCount(vertexDataSize);
  if (vertexBuffer != nullptr) {
    renderPass->bindBuffers(nullptr, vertexBuffer);
    renderPass->draw(PrimitiveType::Triangles, 0, vertexCount);
  } else {
    renderPass->bindBuffers(nullptr, vertexData);
    renderPass->draw(PrimitiveType::TriangleStrip, 0, vertexCount);
  }
}
}  // namespace tgfx
