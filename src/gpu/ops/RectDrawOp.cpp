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

#include "RectDrawOp.h"
#include "gpu/Gpu.h"
#include "gpu/Quad.h"
#include "gpu/ResourceProvider.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "tgfx/core/Buffer.h"

namespace tgfx {
class RectCoverageVerticesProvider : public DataProvider {
 public:
  RectCoverageVerticesProvider(std::vector<RectPaint> rectPaints, bool hasColor)
      : rectPaints(std::move(rectPaints)), hasColor(hasColor) {
  }

  std::shared_ptr<Data> getData() const override {
    auto floatCount = rectPaints.size() * 2 * 4 * (hasColor ? 9 : 5);
    Buffer buffer(floatCount * sizeof(float));
    auto vertices = reinterpret_cast<float*>(buffer.data());
    auto index = 0;
    for (auto& rectPaint : rectPaints) {
      auto& viewMatrix = rectPaint.viewMatrix;
      auto& rect = rectPaint.rect;
      auto scale = sqrtf(viewMatrix.getScaleX() * viewMatrix.getScaleX() +
                         viewMatrix.getSkewY() * viewMatrix.getSkewY());
      // we want the new edge to be .5px away from the old line.
      auto padding = 0.5f / scale;
      auto insetBounds = rect.makeInset(padding, padding);
      auto insetQuad = Quad::MakeFrom(insetBounds, &viewMatrix);
      auto outsetBounds = rect.makeOutset(padding, padding);
      auto outsetQuad = Quad::MakeFrom(outsetBounds, &viewMatrix);

      auto normalInsetQuad = Quad::MakeFrom(insetBounds);
      auto normalOutsetQuad = Quad::MakeFrom(outsetBounds);

      for (int j = 0; j < 2; ++j) {
        const auto& quad = j == 0 ? insetQuad : outsetQuad;
        const auto& normalQuad = j == 0 ? normalInsetQuad : normalOutsetQuad;
        auto coverage = j == 0 ? 1.0f : 0.0f;
        for (size_t k = 0; k < 4; ++k) {
          vertices[index++] = quad.point(k).x;
          vertices[index++] = quad.point(k).y;
          vertices[index++] = coverage;
          vertices[index++] = normalQuad.point(k).x;
          vertices[index++] = normalQuad.point(k).y;
          if (hasColor) {
            auto& color = rectPaint.color;
            vertices[index++] = color.red;
            vertices[index++] = color.green;
            vertices[index++] = color.blue;
            vertices[index++] = color.alpha;
          }
        }
      }
    }
    return buffer.release();
  }

 private:
  std::vector<RectPaint> rectPaints = {};
  bool hasColor = false;
};

class RectNonCoverageVerticesProvider : public DataProvider {
 public:
  RectNonCoverageVerticesProvider(std::vector<RectPaint> rectPaints, bool hasColor)
      : rectPaints(std::move(rectPaints)), hasColor(hasColor) {
  }

  std::shared_ptr<Data> getData() const override {
    auto floatCount = rectPaints.size() * 4 * (hasColor ? 8 : 4);
    Buffer buffer(floatCount * sizeof(float));
    auto vertices = reinterpret_cast<float*>(buffer.data());
    auto index = 0;
    for (auto& rectPaint : rectPaints) {
      auto& viewMatrix = rectPaint.viewMatrix;
      auto& rect = rectPaint.rect;
      auto quad = Quad::MakeFrom(rect, &viewMatrix);
      auto localQuad = Quad::MakeFrom(rect);
      for (size_t j = 4; j >= 1; --j) {
        vertices[index++] = quad.point(j - 1).x;
        vertices[index++] = quad.point(j - 1).y;
        vertices[index++] = localQuad.point(j - 1).x;
        vertices[index++] = localQuad.point(j - 1).y;
        if (hasColor) {
          auto& color = rectPaint.color;
          vertices[index++] = color.red;
          vertices[index++] = color.green;
          vertices[index++] = color.blue;
          vertices[index++] = color.alpha;
        }
      }
    }
    return buffer.release();
  }

 private:
  std::vector<RectPaint> rectPaints = {};
  bool hasColor = false;
};

std::unique_ptr<RectDrawOp> RectDrawOp::Make(Context* context, const std::vector<RectPaint>& rects,
                                             AAType aaType, uint32_t renderFlags) {
  auto bounds = Rect::MakeEmpty();
  for (auto& rectPaint : rects) {
    auto rect = rectPaint.viewMatrix.mapRect(rectPaint.rect);
    bounds.join(rect);
  }
  if (bounds.isEmpty()) {
    return nullptr;
  }
  auto drawOp = std::unique_ptr<RectDrawOp>(new RectDrawOp(bounds, aaType, rects.size()));
  auto& firstColor = rects[0].color;
  std::optional<Color> uniformColor = firstColor;
  for (auto& rectPaint : rects) {
    if (rectPaint.color != firstColor) {
      uniformColor = std::nullopt;
      break;
    }
  }
  drawOp->uniformColor = uniformColor;
  if (aaType == AAType::Coverage) {
    drawOp->indexBufferProxy = context->resourceProvider()->aaQuadIndexBuffer();
  } else if (rects.size() > 1) {
    drawOp->indexBufferProxy = context->resourceProvider()->nonAAQuadIndexBuffer();
  }
  std::unique_ptr<DataProvider> dataProvider = nullptr;
  if (aaType == AAType::Coverage) {
    dataProvider = std::make_unique<RectCoverageVerticesProvider>(rects, !uniformColor.has_value());
  } else {
    dataProvider =
        std::make_unique<RectNonCoverageVerticesProvider>(rects, !uniformColor.has_value());
  }
  if (rects.size() > 1) {
    drawOp->vertexBufferProxy =
        GpuBufferProxy::MakeFrom(context, std::move(dataProvider), BufferType::Vertex, renderFlags);
  } else {
    // If we only have one rect, it is not worth the async task overhead.
    drawOp->vertexData = dataProvider->getData();
  }
  return drawOp;
}

RectDrawOp::RectDrawOp(const Rect& bounds, AAType aaType, size_t rectCount)
    : DrawOp(bounds, aaType), rectCount(rectCount) {
}

void RectDrawOp::execute(RenderPass* renderPass) {
  std::shared_ptr<GpuBuffer> indexBuffer;
  if (indexBufferProxy) {
    indexBuffer = indexBufferProxy->getBuffer();
    if (indexBuffer == nullptr) {
      return;
    }
  }
  std::shared_ptr<GpuBuffer> vertexBuffer;
  if (vertexBufferProxy) {
    vertexBuffer = vertexBufferProxy->getBuffer();
    if (vertexBuffer == nullptr) {
      return;
    }
  } else if (vertexData == nullptr) {
    return;
  }
  auto pipeline =
      createPipeline(renderPass, QuadPerEdgeAAGeometryProcessor::Make(
                                     renderPass->renderTarget()->width(),
                                     renderPass->renderTarget()->height(), aaType, uniformColor));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  if (vertexBuffer) {
    renderPass->bindBuffers(indexBuffer, vertexBuffer);
  } else {
    renderPass->bindBuffers(indexBuffer, vertexData);
  }
  if (indexBuffer != nullptr) {
    uint16_t numIndicesPerQuad;
    if (aaType == AAType::Coverage) {
      numIndicesPerQuad = ResourceProvider::NumIndicesPerAAQuad();
    } else {
      numIndicesPerQuad = ResourceProvider::NumIndicesPerNonAAQuad();
    }
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}
}  // namespace tgfx
