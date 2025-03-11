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
#include "core/DataSource.h"
#include "gpu/Quad.h"
#include "gpu/ResourceProvider.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Data.h"

namespace tgfx {
static void WriteUByte4Color(float* vertices, int& index, const Color& color) {
  auto bytes = reinterpret_cast<uint8_t*>(&vertices[index++]);
  bytes[0] = static_cast<uint8_t>(color.red * 255);
  bytes[1] = static_cast<uint8_t>(color.green * 255);
  bytes[2] = static_cast<uint8_t>(color.blue * 255);
  bytes[3] = static_cast<uint8_t>(color.alpha * 255);
}

class RectCoverageVerticesProvider : public DataSource<Data> {
 public:
  RectCoverageVerticesProvider(PlacementList<RectPaint> rectPaints, bool hasColor, bool useUVCoord)
      : rectPaints(std::move(rectPaints)), hasColor(hasColor), useUVCoord(useUVCoord) {
  }

  std::shared_ptr<Data> getData() const override {
    size_t perVertexCount = useUVCoord ? 5 : 3;
    if (hasColor) {
      perVertexCount += 1;
    }
    auto floatCount = rectPaints.size() * 2 * 4 * perVertexCount;
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
      auto uvInsetQuad = Quad::MakeFrom(insetBounds);
      auto uvOutsetQuad = Quad::MakeFrom(outsetBounds);

      for (int j = 0; j < 2; ++j) {
        const auto& quad = j == 0 ? insetQuad : outsetQuad;
        const auto& uvQuad = j == 0 ? uvInsetQuad : uvOutsetQuad;
        auto coverage = j == 0 ? 1.0f : 0.0f;
        for (size_t k = 0; k < 4; ++k) {
          vertices[index++] = quad.point(k).x;
          vertices[index++] = quad.point(k).y;
          vertices[index++] = coverage;
          if (useUVCoord) {
            vertices[index++] = uvQuad.point(k).x;
            vertices[index++] = uvQuad.point(k).y;
          }
          if (hasColor) {
            WriteUByte4Color(vertices, index, rectPaint.color);
          }
        }
      }
    }
    return buffer.release();
  }

 private:
  PlacementList<RectPaint> rectPaints;
  bool hasColor = false;
  bool useUVCoord = false;
};

class RectNonCoverageVerticesProvider : public DataSource<Data> {
 public:
  RectNonCoverageVerticesProvider(PlacementList<RectPaint> rectPaints, bool hasColor,
                                  bool useUVCoord)
      : rectPaints(std::move(rectPaints)), hasColor(hasColor), useUVCoord(useUVCoord) {
  }

  std::shared_ptr<Data> getData() const override {
    size_t perVertexCount = useUVCoord ? 4 : 2;
    if (hasColor) {
      perVertexCount += 1;
    }
    auto floatCount = rectPaints.size() * 4 * perVertexCount;
    Buffer buffer(floatCount * sizeof(float));
    auto vertices = reinterpret_cast<float*>(buffer.data());
    auto index = 0;
    for (auto& rectPaint : rectPaints) {
      auto& viewMatrix = rectPaint.viewMatrix;
      auto& rect = rectPaint.rect;
      auto quad = Quad::MakeFrom(rect, &viewMatrix);
      auto uvQuad = Quad::MakeFrom(rect);
      for (size_t j = 4; j >= 1; --j) {
        vertices[index++] = quad.point(j - 1).x;
        vertices[index++] = quad.point(j - 1).y;
        if (useUVCoord) {
          vertices[index++] = uvQuad.point(j - 1).x;
          vertices[index++] = uvQuad.point(j - 1).y;
        }
        if (hasColor) {
          WriteUByte4Color(vertices, index, rectPaint.color);
        }
      }
    }
    return buffer.release();
  }

 private:
  PlacementList<RectPaint> rectPaints;
  bool hasColor = false;
  bool useUVCoord = false;
};

PlacementNode<RectDrawOp> RectDrawOp::Make(Context* context, PlacementList<RectPaint> rects,
                                           bool useUVCoord, AAType aaType, uint32_t renderFlags) {
  if (rects.empty()) {
    return nullptr;
  }
  auto rectSize = rects.size();
  auto drawingBuffer = context->drawingBuffer();
  auto drawOp = drawingBuffer->makeNode<RectDrawOp>(aaType, rectSize, useUVCoord);
  auto& firstColor = rects.front().color;
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
  } else if (rectSize > 1) {
    drawOp->indexBufferProxy = context->resourceProvider()->nonAAQuadIndexBuffer();
  }
  std::unique_ptr<DataSource<Data>> source = nullptr;
  if (aaType == AAType::Coverage) {
    source = std::make_unique<RectCoverageVerticesProvider>(std::move(rects),
                                                            !uniformColor.has_value(), useUVCoord);
  } else {
    source = std::make_unique<RectNonCoverageVerticesProvider>(
        std::move(rects), !uniformColor.has_value(), useUVCoord);
  }
  if (rectSize > 1) {
    drawOp->vertexBufferProxy =
        GpuBufferProxy::MakeFrom(context, std::move(source), BufferType::Vertex, renderFlags);
  } else {
    // If we only have one rect, it is not worth the async task overhead.
    drawOp->vertexData = source->getData();
  }
  return drawOp;
}

RectDrawOp::RectDrawOp(AAType aaType, size_t rectCount, bool useUVCoord)
    : DrawOp(aaType), rectCount(rectCount), useUVCoord(useUVCoord) {
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
  auto pipeline = createPipeline(
      renderPass, QuadPerEdgeAAGeometryProcessor::Make(renderPass->renderTarget()->width(),
                                                       renderPass->renderTarget()->height(), aaType,
                                                       uniformColor, useUVCoord));
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
