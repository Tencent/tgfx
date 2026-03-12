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
#include "core/ColorSpaceXformSteps.h"
#include "core/PathTriangulator.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {

struct InstanceRecord {
  float matrixCol0[2];  // [scaleX, skewY]
  float matrixCol1[2];  // [skewX, scaleY]
  float matrixCol2[2];  // [transX, transY]
};

struct InstanceRecordWithColor {
  float matrixCol0[2];
  float matrixCol1[2];
  float matrixCol2[2];
  uint32_t color;  // UByte4Normalized premultiplied RGBA
};

static void FillInstanceRecord(void* buffer, const Matrix& matrix) {
  auto record = static_cast<InstanceRecord*>(buffer);
  record->matrixCol0[0] = matrix.getScaleX();
  record->matrixCol0[1] = matrix.getSkewY();
  record->matrixCol1[0] = matrix.getSkewX();
  record->matrixCol1[1] = matrix.getScaleY();
  record->matrixCol2[0] = matrix.getTranslateX();
  record->matrixCol2[1] = matrix.getTranslateY();
}

PlacementPtr<ShapeInstancedDrawOp> ShapeInstancedDrawOp::Make(
    std::shared_ptr<GPUShapeProxy> shapeProxy, const Matrix* matrices, const Color* colors,
    size_t count, PMColor gpColor, const Matrix& uvMatrix, const Matrix& stateMatrix, AAType aaType,
    std::shared_ptr<ColorSpace> dstColorSpace) {
  if (shapeProxy == nullptr || matrices == nullptr || count == 0) {
    return nullptr;
  }
  auto context = shapeProxy->getContext();
  bool hasColors = colors != nullptr;
  size_t instanceStride = hasColors ? sizeof(InstanceRecordWithColor) : sizeof(InstanceRecord);
  size_t instanceBufferSize = instanceStride * count;
  void* instanceData = nullptr;
  auto instanceBufferProxy =
      context->proxyProvider()->createInstanceBufferProxy(instanceBufferSize, &instanceData);
  if (instanceBufferProxy == nullptr) {
    return nullptr;
  }
  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (hasColors && NeedConvertColorSpace(ColorSpace::SRGB(), dstColorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                               dstColorSpace.get(), AlphaType::Premultiplied);
  }
  auto buffer = instanceData;
  for (size_t i = 0; i < count; i++) {
    FillInstanceRecord(buffer, matrices[i]);
    if (hasColors) {
      auto colorRecord = static_cast<InstanceRecordWithColor*>(buffer);
      colorRecord->color = ToUintPMColor(colors[i], steps.get());
    }
    buffer = static_cast<uint8_t*>(buffer) + instanceStride;
  }
  auto drawingAllocator = context->drawingAllocator();
  return drawingAllocator->make<ShapeInstancedDrawOp>(
      drawingAllocator, std::move(shapeProxy), std::move(instanceBufferProxy), hasColors, count,
      gpColor, uvMatrix, stateMatrix, aaType);
}

ShapeInstancedDrawOp::ShapeInstancedDrawOp(BlockAllocator* allocator,
                                           std::shared_ptr<GPUShapeProxy> proxy,
                                           std::shared_ptr<VertexBufferView> instanceBuffer,
                                           bool hasInstanceColors, size_t count, PMColor gpColor,
                                           const Matrix& uvMatrix, const Matrix& stateMatrix,
                                           AAType aaType)
    : DrawOp(allocator, aaType), shapeProxy(std::move(proxy)),
      instanceBufferProxy(std::move(instanceBuffer)), hasInstanceColors(hasInstanceColors),
      instanceCount(count), gpColor(gpColor), uvMatrix(uvMatrix), stateMatrix(stateMatrix) {
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
  bool hasShader = !colors.empty();
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
    static SamplingArgs args(TileMode::Clamp, TileMode::Clamp,
                             SamplingOptions(FilterMode::Nearest, MipmapMode::None),
                             SrcRectConstraint::Fast);
    auto maskFP = TextureEffect::Make(allocator, std::move(textureProxy), args, &maskMatrix, true);
    if (maskFP == nullptr) {
      return nullptr;
    }
    addCoverageFP(std::move(maskFP));
  }
  return ShapeInstancedGeometryProcessor::Make(allocator, gpColor, renderTarget->width(),
                                               renderTarget->height(), aa, hasInstanceColors,
                                               hasShader, realUVMatrix, stateMatrix);
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
  return true;
}

}  // namespace tgfx
