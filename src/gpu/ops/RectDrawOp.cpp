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
#include "tgfx/core/RenderFlags.h"
#include "profileClient/Profile.h"

namespace tgfx {
class RectPaint {
 public:
  RectPaint(std::optional<Color> color, const Rect& rect, const Matrix& viewMatrix,
            const Matrix* uvMatrix)
      : color(color.value_or(Color::White())), rect(rect), viewMatrix(viewMatrix),
        uvMatrix(uvMatrix ? *uvMatrix : Matrix::I()) {
  }

  Color color;
  Rect rect;
  Matrix viewMatrix;
  Matrix uvMatrix;
};

class RectCoverageVerticesProvider : public DataProvider {
 public:
  RectCoverageVerticesProvider(std::vector<std::shared_ptr<RectPaint>> rectPaints, bool hasColor)
      : rectPaints(std::move(rectPaints)), hasColor(hasColor) {
  }

  std::shared_ptr<Data> getData() const override {
    auto floatCount = rectPaints.size() * 2 * 4 * (hasColor ? 9 : 5);
    Buffer buffer(floatCount * sizeof(float));
    auto vertices = reinterpret_cast<float*>(buffer.data());
    auto index = 0;
    for (auto& rectPaint : rectPaints) {
      auto& viewMatrix = rectPaint->viewMatrix;
      auto& rect = rectPaint->rect;
      auto& uvMatrix = rectPaint->uvMatrix;
      auto scale = sqrtf(viewMatrix.getScaleX() * viewMatrix.getScaleX() +
                         viewMatrix.getSkewY() * viewMatrix.getSkewY());
      // we want the new edge to be .5px away from the old line.
      auto padding = 0.5f / scale;
      auto insetBounds = rect.makeInset(padding, padding);
      auto insetQuad = Quad::MakeFrom(insetBounds, &viewMatrix);
      auto outsetBounds = rect.makeOutset(padding, padding);
      auto outsetQuad = Quad::MakeFrom(outsetBounds, &viewMatrix);

      auto normalInsetQuad = Quad::MakeFrom(insetBounds, &uvMatrix);
      auto normalOutsetQuad = Quad::MakeFrom(outsetBounds, &uvMatrix);

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
            auto& color = rectPaint->color;
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
  std::vector<std::shared_ptr<RectPaint>> rectPaints;
  bool hasColor;
};

class RectNonCoverageVerticesProvider : public DataProvider {
 public:
  RectNonCoverageVerticesProvider(std::vector<std::shared_ptr<RectPaint>> rectPaints, bool hasColor)
      : rectPaints(std::move(rectPaints)), hasColor(hasColor) {
  }

  std::shared_ptr<Data> getData() const override {
    auto floatCount = rectPaints.size() * 4 * (hasColor ? 8 : 4);
    Buffer buffer(floatCount * sizeof(float));
    auto vertices = reinterpret_cast<float*>(buffer.data());
    auto index = 0;
    for (auto& rectPaint : rectPaints) {
      auto& viewMatrix = rectPaint->viewMatrix;
      auto& rect = rectPaint->rect;
      auto& uvMatrix = rectPaint->uvMatrix;
      auto quad = Quad::MakeFrom(rect, &viewMatrix);
      auto localQuad = Quad::MakeFrom(rect, &uvMatrix);
      for (size_t j = 4; j >= 1; --j) {
        vertices[index++] = quad.point(j - 1).x;
        vertices[index++] = quad.point(j - 1).y;
        vertices[index++] = localQuad.point(j - 1).x;
        vertices[index++] = localQuad.point(j - 1).y;
        if (hasColor) {
          auto& color = rectPaint->color;
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
  std::vector<std::shared_ptr<RectPaint>> rectPaints;
  bool hasColor;
};

std::unique_ptr<RectDrawOp> RectDrawOp::Make(std::optional<Color> color, const Rect& rect,
                                             const Matrix& viewMatrix, const Matrix* uvMatrix) {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("RectDrawOp::Make");
  return std::unique_ptr<RectDrawOp>(new RectDrawOp(color, rect, viewMatrix, uvMatrix));
}

RectDrawOp::RectDrawOp(std::optional<Color> color, const Rect& rect, const Matrix& viewMatrix,
                       const Matrix* uvMatrix)
    : DrawOp(ClassID()), hasColor(color) {
  auto rectPaint = std::make_shared<RectPaint>(color, rect, viewMatrix, uvMatrix);
  rectPaints.push_back(std::move(rectPaint));
  auto bounds = viewMatrix.mapRect(rect);
  setBounds(bounds);
}

bool RectDrawOp::canAdd(size_t count) const {
  return rectPaints.size() + count <=
         static_cast<size_t>(aa == AAType::Coverage ? ResourceProvider::MaxNumAAQuads()
                                                    : ResourceProvider::MaxNumNonAAQuads());
}

bool RectDrawOp::onCombineIfPossible(Op* op) {
  auto* that = static_cast<RectDrawOp*>(op);
  if (hasColor != that->hasColor || !canAdd(that->rectPaints.size()) ||
      !DrawOp::onCombineIfPossible(op)) {
    return false;
  }
  rectPaints.insert(rectPaints.end(), that->rectPaints.begin(), that->rectPaints.end());
  return true;
}

bool RectDrawOp::needsIndexBuffer() const {
  return rectPaints.size() > 1 || aa == AAType::Coverage;
}

void RectDrawOp::prepare(Context* context, uint32_t renderFlags) {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("RectDrawOp::prepare");
  if (needsIndexBuffer()) {
    if (aa == AAType::Coverage) {
      indexBufferProxy = context->resourceProvider()->aaQuadIndexBuffer();
    } else {
      indexBufferProxy = context->resourceProvider()->nonAAQuadIndexBuffer();
    }
  }
  std::shared_ptr<DataProvider> dataProvider = nullptr;
  if (aa == AAType::Coverage) {
    dataProvider = std::make_shared<RectCoverageVerticesProvider>(rectPaints, hasColor);
  } else {
    dataProvider = std::make_shared<RectNonCoverageVerticesProvider>(rectPaints, hasColor);
  }
  if (rectPaints.size() > 1) {
    vertexBufferProxy =
        GpuBufferProxy::MakeFrom(context, std::move(dataProvider), BufferType::Vertex, renderFlags);
  } else {
    // If we only have one rect, it is not worth the async task overhead.
    vertexData = dataProvider->getData();
  }
}

void RectDrawOp::execute(RenderPass* renderPass) {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("RectDrawOp::execute");
  std::shared_ptr<GpuBuffer> indexBuffer;
  if (needsIndexBuffer()) {
    if (indexBufferProxy == nullptr) {
      return;
    }
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
      renderPass,
      QuadPerEdgeAAGeometryProcessor::Make(renderPass->renderTarget()->width(),
                                           renderPass->renderTarget()->height(), aa, hasColor));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  if (vertexBuffer) {
    renderPass->bindBuffers(indexBuffer, vertexBuffer);
  } else {
    renderPass->bindBuffers(indexBuffer, vertexData);
  }
  if (indexBuffer != nullptr) {
    uint16_t numIndicesPerQuad;
    if (aa == AAType::Coverage) {
      numIndicesPerQuad = ResourceProvider::NumIndicesPerAAQuad();
    } else {
      numIndicesPerQuad = ResourceProvider::NumIndicesPerNonAAQuad();
    }
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectPaints.size() * numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}
}  // namespace tgfx
