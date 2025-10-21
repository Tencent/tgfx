/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "gpu/RectsVertexProvider.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<ShapeDrawOp> ShapeDrawOp::Make(std::shared_ptr<GPUShapeProxy> shapeProxy, Color color,
                                            const Matrix& uvMatrix, AAType aaType) {
  if (shapeProxy == nullptr) {
    return nullptr;
  }
  auto drawingBuffer = shapeProxy->getContext()->drawingBuffer();
  return drawingBuffer->make<ShapeDrawOp>(std::move(shapeProxy), color, uvMatrix, aaType);
}

ShapeDrawOp::ShapeDrawOp(std::shared_ptr<GPUShapeProxy> proxy, Color color, const Matrix& uvMatrix,
                         AAType aaType)
    : DrawOp(aaType), shapeProxy(std::move(proxy)), color(color), uvMatrix(uvMatrix) {
  auto context = shapeProxy->getContext();
  if (auto textureProxy = shapeProxy->getTextureProxy()) {
    auto maskRect = Rect::MakeWH(textureProxy->width(), textureProxy->height());
    auto maskVertexProvider =
        RectsVertexProvider::MakeFrom(context->drawingBuffer(), maskRect, AAType::None);
    maskBufferProxy = context->proxyProvider()->createVertexBufferProxy(
        std::move(maskVertexProvider), RenderFlags::DisableAsyncTask);
  }
}

PlacementPtr<GeometryProcessor> ShapeDrawOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("color", color);
  ATTRIBUTE_NAME("uvMatrix", uvMatrix);
  if (shapeProxy == nullptr) {
    return nullptr;
  }
  auto viewMatrix = shapeProxy->getDrawingMatrix();
  auto realUVMatrix = uvMatrix;
  realUVMatrix.preConcat(viewMatrix);
  auto vertexBuffer = shapeProxy->getTriangles();
  auto aa = aaType;
  if (vertexBuffer == nullptr) {
    aa = AAType::None;
    auto textureProxy = shapeProxy->getTextureProxy();
    if (textureProxy == nullptr || maskBufferProxy == nullptr ||
        maskBufferProxy->getBuffer() == nullptr) {
      return nullptr;
    }
    Matrix maskMatrix = {};
    if (!realUVMatrix.invert(&maskMatrix)) {
      return nullptr;
    }
    static SamplingArgs args(TileMode::Clamp, TileMode::Clamp,
                             SamplingOptions(FilterMode::Nearest, MipmapMode::None),
                             SrcRectConstraint::Fast);
    auto maskFP = TextureEffect::Make(std::move(textureProxy), args, &maskMatrix, true);
    if (maskFP == nullptr) {
      return nullptr;
    }
    addCoverageFP(std::move(maskFP));
  }
  auto drawingBuffer = renderTarget->getContext()->drawingBuffer();
  return DefaultGeometryProcessor::Make(drawingBuffer, color, renderTarget->width(),
                                        renderTarget->height(), aa, viewMatrix, realUVMatrix,
                                        renderTarget->colorSpace());
}

void ShapeDrawOp::onDraw(RenderPass* renderPass) {
  auto vertexBuffer = shapeProxy->getTriangles();
  if (vertexBuffer != nullptr) {
    renderPass->setVertexBuffer(vertexBuffer->gpuBuffer());
    auto vertexCount = aaType == AAType::Coverage
                           ? PathTriangulator::GetAATriangleCount(vertexBuffer->size())
                           : PathTriangulator::GetTriangleCount(vertexBuffer->size());
    renderPass->draw(PrimitiveType::Triangles, 0, vertexCount);
  } else {
    auto maskBuffer = maskBufferProxy->getBuffer();
    renderPass->setVertexBuffer(maskBuffer->gpuBuffer(), maskBufferProxy->offset());
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}

bool ShapeDrawOp::hasCoverage() const {
  return true;
}

}  // namespace tgfx
