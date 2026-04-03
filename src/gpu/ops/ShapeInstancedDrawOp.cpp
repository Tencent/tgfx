/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "ShapeInstancedDrawOp.h"
#include "core/PathTriangulator.h"
#include "core/utils/Log.h"
#include "gpu/InstanceProvider.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {

PlacementPtr<ShapeInstancedDrawOp> ShapeInstancedDrawOp::Make(
    std::shared_ptr<GPUShapeProxy> shapeProxy, const Point* offsets, const Color* colors,
    size_t count, const Matrix& uvMatrix, const Matrix& stateMatrix, AAType aaType,
    const std::shared_ptr<ColorSpace>& dstColorSpace) {
  if (shapeProxy == nullptr || offsets == nullptr || count == 0) {
    return nullptr;
  }
  auto context = shapeProxy->getContext();
  auto drawingAllocator = context->drawingAllocator();
  auto provider =
      InstanceProvider::MakeFrom(drawingAllocator, offsets, colors, count, dstColorSpace);
  if (provider == nullptr) {
    return nullptr;
  }
  bool hasColors = provider->hasColors();
  auto instanceBufferProxy =
      context->proxyProvider()->createInstanceBufferProxy(std::move(provider));
  if (instanceBufferProxy == nullptr) {
    return nullptr;
  }
  return drawingAllocator->make<ShapeInstancedDrawOp>(drawingAllocator, std::move(shapeProxy),
                                                      std::move(instanceBufferProxy), hasColors,
                                                      count, uvMatrix, stateMatrix, aaType);
}

ShapeInstancedDrawOp::ShapeInstancedDrawOp(BlockAllocator* allocator,
                                           std::shared_ptr<GPUShapeProxy> proxy,
                                           std::shared_ptr<VertexBufferView> instanceBuffer,
                                           bool hasInstanceColors, size_t count,
                                           const Matrix& uvMatrix, const Matrix& stateMatrix,
                                           AAType aaType)
    : DrawOp(allocator, aaType), shapeProxy(std::move(proxy)),
      instanceBufferProxy(std::move(instanceBuffer)), hasInstanceColors(hasInstanceColors),
      instanceCount(count), uvMatrix(uvMatrix), stateMatrix(stateMatrix) {
  auto context = shapeProxy->getContext();
  if (auto textureProxy = shapeProxy->getTextureProxy()) {
    auto maskRect = Rect::MakeWH(textureProxy->width(), textureProxy->height());
    auto maskVertexProvider = RectsVertexProvider::MakeFrom(allocator, maskRect, AAType::None);
    maskBufferProxy = context->proxyProvider()->createVertexBufferProxy(
        std::move(maskVertexProvider), RenderFlags::DisableAsyncTask);
  }
}

PlacementPtr<GeometryProcessor> ShapeInstancedDrawOp::onMakeGeometryProcessor(
    RenderTarget* renderTarget) {
  if (shapeProxy == nullptr) {
    return nullptr;
  }
  auto realUVMatrix = uvMatrix;
  realUVMatrix.preConcat(shapeProxy->getDrawingMatrix());
  auto aa = aaType;
  auto vertexBuffer = shapeProxy->getTriangles();
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
    static const SamplingArgs args(TileMode::Clamp, TileMode::Clamp,
                                   SamplingOptions(FilterMode::Nearest, MipmapMode::None),
                                   SrcRectConstraint::Fast);
    auto maskFP = TextureEffect::Make(allocator, std::move(textureProxy), args, &maskMatrix, true);
    if (maskFP == nullptr) {
      return nullptr;
    }
    addCoverageFP(std::move(maskFP));
  }
  auto viewMatrix = stateMatrix * realUVMatrix;
  return ShapeInstancedGeometryProcessor::Make(allocator, renderTarget->width(),
                                               renderTarget->height(), aa, hasInstanceColors,
                                               realUVMatrix, viewMatrix);
}

void ShapeInstancedDrawOp::onDraw(RenderPass* renderPass) {
  auto vertexBuffer = shapeProxy->getTriangles();
  auto instanceBuffer = instanceBufferProxy->getBuffer();
  if (instanceBuffer == nullptr) {
    LOGE("ShapeInstancedDrawOp::onDraw() Failed to get instance buffer!");
    return;
  }
  if (vertexBuffer != nullptr) {
    renderPass->setVertexBuffer(0, vertexBuffer->gpuBuffer());
    renderPass->setVertexBuffer(1, instanceBuffer->gpuBuffer(), instanceBufferProxy->offset());
    auto vertexCount = aaType == AAType::Coverage
                           ? PathTriangulator::GetAAVertexCount(vertexBuffer->size())
                           : PathTriangulator::GetNonAAVertexCount(vertexBuffer->size());
    renderPass->draw(PrimitiveType::Triangles, static_cast<uint32_t>(vertexCount),
                     static_cast<uint32_t>(instanceCount));
  } else {
    auto maskBuffer = maskBufferProxy->getBuffer();
    renderPass->setVertexBuffer(0, maskBuffer->gpuBuffer(), maskBufferProxy->offset());
    renderPass->setVertexBuffer(1, instanceBuffer->gpuBuffer(), instanceBufferProxy->offset());
    renderPass->draw(PrimitiveType::TriangleStrip, 4, static_cast<uint32_t>(instanceCount));
  }
}

bool ShapeInstancedDrawOp::hasCoverage() const {
  return aaType == AAType::Coverage || DrawOp::hasCoverage();
}

}  // namespace tgfx
