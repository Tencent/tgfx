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
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/ResourceProvider.h"
#include "gpu/VertexProvider.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
static void WriteUByte4Color(float* vertices, int& index, const Color& color) {
  auto bytes = reinterpret_cast<uint8_t*>(&vertices[index++]);
  bytes[0] = static_cast<uint8_t>(color.red * 255);
  bytes[1] = static_cast<uint8_t>(color.green * 255);
  bytes[2] = static_cast<uint8_t>(color.blue * 255);
  bytes[3] = static_cast<uint8_t>(color.alpha * 255);
}

class RectCoverageVertexProvider : public VertexProvider {
 public:
  RectCoverageVertexProvider(std::vector<PlacementPtr<RectPaint>> rectPaints, bool hasColor,
                             bool useUVCoord)
      : rectPaints(std::move(rectPaints)), hasColor(hasColor), useUVCoord(useUVCoord) {
  }

  size_t vertexCount() const override {
    size_t perVertexCount = useUVCoord ? 5 : 3;
    if (hasColor) {
      perVertexCount += 1;
    }
    return rectPaints.size() * 2 * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    for (auto& rectPaint : rectPaints) {
      auto& viewMatrix = rectPaint->viewMatrix;
      auto& rect = rectPaint->rect;
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
        auto& quad = j == 0 ? insetQuad : outsetQuad;
        auto& uvQuad = j == 0 ? uvInsetQuad : uvOutsetQuad;
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
            WriteUByte4Color(vertices, index, rectPaint->color);
          }
        }
      }
    }
  }

 private:
  std::vector<PlacementPtr<RectPaint>> rectPaints = {};
  bool hasColor = false;
  bool useUVCoord = false;
};

class RectNonCoverageVertexProvider : public VertexProvider {
 public:
  RectNonCoverageVertexProvider(std::vector<PlacementPtr<RectPaint>> rectPaints, bool hasColor,
                                bool useUVCoord)
      : rectPaints(std::move(rectPaints)), hasColor(hasColor), useUVCoord(useUVCoord) {
  }

  size_t vertexCount() const override {
    size_t perVertexCount = useUVCoord ? 4 : 2;
    if (hasColor) {
      perVertexCount += 1;
    }
    return rectPaints.size() * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    for (auto& rectPaint : rectPaints) {
      auto& viewMatrix = rectPaint->viewMatrix;
      auto& rect = rectPaint->rect;
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
          WriteUByte4Color(vertices, index, rectPaint->color);
        }
      }
    }
  }

 private:
  std::vector<PlacementPtr<RectPaint>> rectPaints = {};
  bool hasColor = false;
  bool useUVCoord = false;
};

PlacementPtr<RectDrawOp> RectDrawOp::Make(Context* context,
                                          std::vector<PlacementPtr<RectPaint>> rects,
                                          bool useUVCoord, AAType aaType, uint32_t renderFlags) {
  if (rects.empty()) {
    return nullptr;
  }
  auto rectSize = rects.size();
  auto drawingBuffer = context->drawingBuffer();
  auto drawOp = drawingBuffer->make<RectDrawOp>(aaType, rectSize, useUVCoord);
  auto& firstColor = rects.front()->color;
  std::optional<Color> uniformColor = firstColor;
  for (auto& rectPaint : rects) {
    if (rectPaint->color != firstColor) {
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
  std::unique_ptr<VertexProvider> provider = nullptr;
  if (aaType == AAType::Coverage) {
    provider = std::make_unique<RectCoverageVertexProvider>(std::move(rects),
                                                            !uniformColor.has_value(), useUVCoord);
  } else {
    provider = std::make_unique<RectNonCoverageVertexProvider>(
        std::move(rects), !uniformColor.has_value(), useUVCoord);
  }
  if (rectSize <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  auto sharedVertexBuffer =
      context->proxyProvider()->createSharedVertexBuffer(std::move(provider), renderFlags);
  drawOp->vertexBufferProxy = sharedVertexBuffer.first;
  drawOp->vertexBufferOffset = sharedVertexBuffer.second;
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
  std::shared_ptr<GpuBuffer> vertexBuffer =
      vertexBufferProxy ? vertexBufferProxy->getBuffer() : nullptr;
  if (vertexBuffer == nullptr) {
    return;
  }
  auto renderTarget = renderPass->renderTarget();
  auto drawingBuffer = renderPass->getContext()->drawingBuffer();
  auto gp = QuadPerEdgeAAGeometryProcessor::Make(drawingBuffer, renderTarget->width(),
                                                 renderTarget->height(), aaType, uniformColor,
                                                 useUVCoord);
  auto pipeline = createPipeline(renderPass, std::move(gp));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(indexBuffer, vertexBuffer, vertexBufferOffset);
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
