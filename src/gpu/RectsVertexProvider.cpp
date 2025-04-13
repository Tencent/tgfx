/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "RectsVertexProvider.h"
#include "core/utils/Log.h"
#include "gpu/Quad.h"

namespace tgfx {
static void WriteUByte4Color(float* vertices, int& index, const Color& color) {
  auto bytes = reinterpret_cast<uint8_t*>(&vertices[index++]);
  bytes[0] = static_cast<uint8_t>(color.red * 255);
  bytes[1] = static_cast<uint8_t>(color.green * 255);
  bytes[2] = static_cast<uint8_t>(color.blue * 255);
  bytes[3] = static_cast<uint8_t>(color.alpha * 255);
}

class AARectsVertexProvider : public RectsVertexProvider {
 public:
  AARectsVertexProvider(std::vector<PlacementPtr<RectPaint>> rects, AAType aaType, bool hasUVCoord,
                        bool hasColor)
      : RectsVertexProvider(std::move(rects), aaType, hasUVCoord, hasColor) {
  }

  size_t vertexCount() const override {
    size_t perVertexCount = bitFields.hasUVCoord ? 5 : 3;
    if (bitFields.hasColor) {
      perVertexCount += 1;
    }
    return rects.size() * 2 * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    for (auto& rectPaint : rects) {
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
          if (bitFields.hasUVCoord) {
            vertices[index++] = uvQuad.point(k).x;
            vertices[index++] = uvQuad.point(k).y;
          }
          if (bitFields.hasColor) {
            WriteUByte4Color(vertices, index, rectPaint->color);
          }
        }
      }
    }
  }
};

class NonAARectVertexProvider : public RectsVertexProvider {
 public:
  NonAARectVertexProvider(std::vector<PlacementPtr<RectPaint>> rects, AAType aaType,
                          bool hasUVCoord, bool hasColor)
      : RectsVertexProvider(std::move(rects), aaType, hasUVCoord, hasColor) {
  }

  size_t vertexCount() const override {
    size_t perVertexCount = bitFields.hasUVCoord ? 4 : 2;
    if (bitFields.hasColor) {
      perVertexCount += 1;
    }
    return rects.size() * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    for (auto& rectPaint : rects) {
      auto& viewMatrix = rectPaint->viewMatrix;
      auto& rect = rectPaint->rect;
      auto quad = Quad::MakeFrom(rect, &viewMatrix);
      auto uvQuad = Quad::MakeFrom(rect);
      for (size_t j = 4; j >= 1; --j) {
        vertices[index++] = quad.point(j - 1).x;
        vertices[index++] = quad.point(j - 1).y;
        if (bitFields.hasUVCoord) {
          vertices[index++] = uvQuad.point(j - 1).x;
          vertices[index++] = uvQuad.point(j - 1).y;
        }
        if (bitFields.hasColor) {
          WriteUByte4Color(vertices, index, rectPaint->color);
        }
      }
    }
  }
};

std::unique_ptr<RectsVertexProvider> RectsVertexProvider::MakeFrom(
    PlacementPtr<tgfx::RectPaint> rect, AAType aaType) {
  if (rect == nullptr) {
    return nullptr;
  }
  auto rects = std::vector<PlacementPtr<RectPaint>>{};
  rects.push_back(std::move(rect));
  return MakeFrom(std::move(rects), aaType, false);
}

std::unique_ptr<RectsVertexProvider> RectsVertexProvider::MakeFrom(
    std::vector<PlacementPtr<RectPaint>> rects, AAType aaType, bool needUVCoord) {
  if (rects.empty()) {
    return nullptr;
  }
  auto hasColor = false;
  auto& firstColor = rects.front()->color;
  for (auto& rectPaint : rects) {
    if (rectPaint->color != firstColor) {
      hasColor = true;
      break;
    }
  }
  if (aaType == AAType::Coverage) {
    return std::make_unique<AARectsVertexProvider>(std::move(rects), aaType, needUVCoord, hasColor);
  }
  return std::make_unique<NonAARectVertexProvider>(std::move(rects), aaType, needUVCoord, hasColor);
}

RectsVertexProvider::RectsVertexProvider(std::vector<PlacementPtr<RectPaint>> rects, AAType aaType,
                                         bool hasUVCoord, bool hasColor)
    : rects(std::move(rects)) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.hasUVCoord = hasUVCoord;
  bitFields.hasColor = hasColor;
}
}  // namespace tgfx
