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
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/ShapeRasterizer.h"
#include "core/utils/StrokeKey.h"
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
std::unique_ptr<ShapeDrawOp> ShapeDrawOp::Make(Color color, const Path& path,
                                               const Matrix& viewMatrix, const Stroke* stroke) {
  if (path.isEmpty()) {
    return nullptr;
  }

  return std::unique_ptr<ShapeDrawOp>(new ShapeDrawOp(color, path, viewMatrix, stroke));
}

ShapeDrawOp::ShapeDrawOp(Color color, Path p, const Matrix& viewMatrix, const Stroke* stroke)
    : DrawOp(ClassID()), color(color), path(std::move(p)), viewMatrix(viewMatrix),
      stroke(stroke ? new Stroke(*stroke) : nullptr) {
  auto bounds = path.getBounds();
  viewMatrix.mapRect(&bounds);
  setBounds(bounds);
}

ShapeDrawOp::~ShapeDrawOp() {
  delete stroke;
}

bool ShapeDrawOp::onCombineIfPossible(Op*) {
  return false;
}

void ShapeDrawOp::prepare(Context* context, uint32_t renderFlags) {
  BytesKey bytesKey = {};
  auto scales = viewMatrix.getAxisScales();
  if (scales.x == scales.y) {
    rasterizeMatrix.setScale(scales.x, scales.y);
    bytesKey.reserve(1 + (stroke ? StrokeKeyCount : 0));
    bytesKey.write(scales.x);
  } else {
    rasterizeMatrix = viewMatrix;
    rasterizeMatrix.setTranslateX(0);
    rasterizeMatrix.setTranslateY(0);
    bytesKey.reserve(4 + (stroke ? StrokeKeyCount : 0));
    bytesKey.write(rasterizeMatrix.getScaleX());
    bytesKey.write(rasterizeMatrix.getSkewX());
    bytesKey.write(rasterizeMatrix.getSkewY());
    bytesKey.write(rasterizeMatrix.getScaleY());
  }
  if (stroke) {
    WriteStrokeKey(&bytesKey, stroke);
  }
  auto uniqueKey = UniqueKey::Append(PathRef::GetUniqueKey(path), bytesKey.data(), bytesKey.size());
  auto bounds = path.getBounds();
  if (stroke) {
    bounds.outset(stroke->width, stroke->width);
  }
  rasterizeMatrix.mapRect(&bounds);
  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto width = ceilf(bounds.width());
  auto height = ceilf(bounds.height());
  auto rasterizer = Rasterizer::MakeFrom(path, ISize::Make(width, height), rasterizeMatrix,
                                         aa == AAType::Coverage, stroke);
  shapeProxy =
      context->proxyProvider()->createGpuShapeProxy(uniqueKey, std::move(rasterizer), renderFlags);
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
  if (shapeProxy == nullptr) {
    return;
  }
  Matrix uvMatrix = {};
  if (!rasterizeMatrix.invert(&uvMatrix)) {
    return;
  }
  auto realViewMatrix = viewMatrix;
  realViewMatrix.preConcat(uvMatrix);
  auto vertexBuffer = shapeProxy->getTriangles();
  std::shared_ptr<Data> vertexData = nullptr;
  if (vertexBuffer == nullptr) {
    auto textureProxy = shapeProxy->getTextureProxy();
    if (textureProxy == nullptr) {
      return;
    }
    auto rect = Rect::MakeWH(textureProxy->width(), textureProxy->height());
    vertexData = aa == AAType::Coverage ? MakeAAVertexData(rect) : MakeVertexData(rect);
    auto maskFP = TextureEffect::Make(std::move(textureProxy), {}, &rasterizeMatrix, true);
    if (maskFP == nullptr) {
      return;
    }
    addCoverageFP(std::move(maskFP));
  }
  auto pipeline = createPipeline(
      renderPass, DefaultGeometryProcessor::Make(color, renderPass->renderTarget()->width(),
                                                 renderPass->renderTarget()->height(), aa,
                                                 realViewMatrix, uvMatrix));
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
