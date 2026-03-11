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
#include "gpu/resources/BufferResource.h"
#include "tgfx/core/RenderFlags.h"
#include "tgfx/gpu/GPU.h"

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

PlacementPtr<ShapeInstancedDrawOp> ShapeInstancedDrawOp::Make(
    std::shared_ptr<GPUShapeProxy> shapeProxy, const Matrix* matrices, const Color* colors,
    size_t count, PMColor gpColor, const Matrix& uvMatrix, const Matrix& stateMatrix, AAType aaType,
    std::shared_ptr<ColorSpace> dstColorSpace) {
  if (shapeProxy == nullptr || matrices == nullptr || count == 0) {
    return nullptr;
  }
  auto allocator = shapeProxy->getContext()->drawingAllocator();
  auto storedMatrices = reinterpret_cast<Matrix*>(allocator->allocate(sizeof(Matrix) * count));
  if (storedMatrices == nullptr) {
    return nullptr;
  }
  memcpy(storedMatrices, matrices, sizeof(Matrix) * count);

  const Color* storedColors = nullptr;
  if (colors != nullptr) {
    auto colorPtr = reinterpret_cast<Color*>(allocator->allocate(sizeof(Color) * count));
    if (colorPtr == nullptr) {
      return nullptr;
    }
    memcpy(colorPtr, colors, sizeof(Color) * count);
    storedColors = colorPtr;
  }
  return allocator->make<ShapeInstancedDrawOp>(allocator, std::move(shapeProxy), storedMatrices,
                                               storedColors, count, gpColor, uvMatrix, stateMatrix,
                                               aaType, std::move(dstColorSpace));
}

ShapeInstancedDrawOp::ShapeInstancedDrawOp(BlockAllocator* allocator,
                                           std::shared_ptr<GPUShapeProxy> proxy,
                                           const Matrix* matrices, const Color* instanceColors,
                                           size_t count, PMColor gpColor, const Matrix& uvMatrix,
                                           const Matrix& stateMatrix, AAType aaType,
                                           std::shared_ptr<ColorSpace> dstColorSpace)
    : DrawOp(allocator, aaType), shapeProxy(std::move(proxy)), matrices(matrices),
      instanceColors(instanceColors), instanceCount(count), gpColor(gpColor), uvMatrix(uvMatrix),
      stateMatrix(stateMatrix), dstColorSpace(std::move(dstColorSpace)) {
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
  bool hasInstanceColors = instanceColors != nullptr;
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
  bool hasInstanceColors = instanceColors != nullptr;
  size_t instanceStride =
      hasInstanceColors ? sizeof(InstanceRecordWithColor) : sizeof(InstanceRecord);
  size_t instanceBufferSize = instanceStride * instanceCount;
  auto bufferResource = BufferResource::FindOrCreate(shapeProxy->getContext(), instanceBufferSize,
                                                     GPUBufferUsage::VERTEX);
  if (bufferResource == nullptr) {
    LOGE("ShapeInstancedDrawOp::onDraw() Failed to create instance buffer!");
    return;
  }
  auto instanceBuffer = bufferResource->gpuBuffer();
  auto buffer = instanceBuffer->map();
  if (buffer == nullptr) {
    LOGE("ShapeInstancedDrawOp::onDraw() Failed to map instance buffer!");
    return;
  }

  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (hasInstanceColors && NeedConvertColorSpace(ColorSpace::SRGB(), dstColorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                               dstColorSpace.get(), AlphaType::Premultiplied);
  }
  for (size_t i = 0; i < instanceCount; i++) {
    auto record = static_cast<InstanceRecord*>(buffer);
    auto& m = matrices[i];
    record->matrixCol0[0] = m.getScaleX();
    record->matrixCol0[1] = m.getSkewY();
    record->matrixCol1[0] = m.getSkewX();
    record->matrixCol1[1] = m.getScaleY();
    record->matrixCol2[0] = m.getTranslateX();
    record->matrixCol2[1] = m.getTranslateY();
    if (hasInstanceColors) {
      auto colorRecord = static_cast<InstanceRecordWithColor*>(buffer);
      colorRecord->color = ToUintPMColor(instanceColors[i], steps.get());
    }
    buffer = static_cast<uint8_t*>(buffer) + instanceStride;
  }
  instanceBuffer->unmap();

  if (vertexBuffer != nullptr) {
    renderPass->setVertexBuffer(0, vertexBuffer->gpuBuffer());
    renderPass->setVertexBuffer(1, std::move(instanceBuffer));
    auto vertexCount = aaType == AAType::Coverage
                           ? PathTriangulator::GetAAVertexCount(vertexBuffer->size())
                           : PathTriangulator::GetNonAAVertexCount(vertexBuffer->size());
    renderPass->draw(PrimitiveType::Triangles, static_cast<uint32_t>(vertexCount),
                     static_cast<uint32_t>(instanceCount));
  } else {
    auto maskBuffer = maskBufferProxy->getBuffer();
    renderPass->setVertexBuffer(0, maskBuffer->gpuBuffer(), maskBufferProxy->offset());
    renderPass->setVertexBuffer(1, std::move(instanceBuffer));
    renderPass->draw(PrimitiveType::TriangleStrip, 4, static_cast<uint32_t>(instanceCount));
  }
}

bool ShapeInstancedDrawOp::hasCoverage() const {
  return true;
}

}  // namespace tgfx
