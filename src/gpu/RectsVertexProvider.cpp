/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "gpu/Quad.h"

namespace tgfx {
inline void WriteUByte4Color(float* vertices, int& index, const Color& color) {
  auto bytes = reinterpret_cast<uint8_t*>(&vertices[index++]);
  bytes[0] = static_cast<uint8_t>(color.red * 255);
  bytes[1] = static_cast<uint8_t>(color.green * 255);
  bytes[2] = static_cast<uint8_t>(color.blue * 255);
  bytes[3] = static_cast<uint8_t>(color.alpha * 255);
}

inline void ApplySubsetMode(UVSubsetMode mode, Rect* rect) {
  if (mode == UVSubsetMode::None) {
    return;
  }
  if (mode == UVSubsetMode::RoundOutAndSubset) {
    rect->roundOut();
  }
  rect->inset(0.5f, 0.5f);
}

inline void WriteSubset(float* vertices, int& index, const Rect& subset) {
  vertices[index++] = subset.left;
  vertices[index++] = subset.top;
  vertices[index++] = subset.right;
  vertices[index++] = subset.bottom;
}

class AARectsVertexProvider : public RectsVertexProvider {
 public:
  AARectsVertexProvider(PlacementArray<RectRecord>&& rects, PlacementArray<Rect>&& uvRects,
                        AAType aaType, bool hasUVCoord, bool hasColor, UVSubsetMode subsetMode,
                        std::shared_ptr<BlockBuffer> reference)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            subsetMode, std::move(reference)) {
  }

  size_t vertexCount() const override {
    size_t perVertexCount = bitFields.hasUVCoord ? 5 : 3;
    if (bitFields.hasColor) {
      perVertexCount += 1;
    }
    if (static_cast<UVSubsetMode>(bitFields.subsetMode) != UVSubsetMode::None) {
      perVertexCount += 4;
    }
    return rects.size() * 2 * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    bool needSubset = static_cast<UVSubsetMode>(bitFields.subsetMode) != UVSubsetMode::None;
    auto hasUVRect = !uvRects.empty();
    auto rectCount = rects.size();
    for (size_t i = 0; i < rectCount; ++i) {
      auto& record = rects[i];
      auto& viewMatrix = record->viewMatrix;
      auto& rect = record->rect;
      auto scale = sqrtf(viewMatrix.getScaleX() * viewMatrix.getScaleX() +
                         viewMatrix.getSkewY() * viewMatrix.getSkewY());
      // we want the new edge to be .5px away from the old line.
      auto padding = 0.5f / scale;
      auto insetBounds = rect.makeInset(padding, padding);
      auto insetQuad = Quad::MakeFrom(insetBounds, &viewMatrix);
      auto outsetBounds = rect.makeOutset(padding, padding);
      auto outsetQuad = Quad::MakeFrom(outsetBounds, &viewMatrix);
      auto insetUV = insetBounds;
      auto outsetUV = outsetBounds;
      auto subset = rect;
      if (hasUVRect) {
        auto& uvRect = *uvRects[i];
        insetUV = uvRect.makeInset(padding, padding);
        outsetUV = uvRect.makeOutset(padding, padding);
        subset = uvRect;
      }
      if (needSubset) {
        ApplySubsetMode(static_cast<UVSubsetMode>(bitFields.subsetMode), &subset);
      }
      auto uvInsetQuad = Quad::MakeFrom(insetUV);
      auto uvOutsetQuad = Quad::MakeFrom(outsetUV);
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
            WriteUByte4Color(vertices, index, record->color);
          }
          if (needSubset) {
            WriteSubset(vertices, index, subset);
          }
        }
      }
    }
  }
};

class NonAARectsVertexProvider : public RectsVertexProvider {
 public:
  NonAARectsVertexProvider(PlacementArray<RectRecord>&& rects, PlacementArray<Rect>&& uvRects,
                           AAType aaType, bool hasUVCoord, bool hasColor, UVSubsetMode subsetMode,
                           std::shared_ptr<BlockBuffer> reference)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            subsetMode, std::move(reference)) {
  }

  size_t vertexCount() const override {
    size_t perVertexCount = bitFields.hasUVCoord ? 4 : 2;
    if (bitFields.hasColor) {
      perVertexCount += 1;
    }
    if (static_cast<UVSubsetMode>(bitFields.subsetMode) != UVSubsetMode::None) {
      perVertexCount += 4;
    }
    return rects.size() * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    bool needSubset = static_cast<UVSubsetMode>(bitFields.subsetMode) != UVSubsetMode::None;
    auto hasUVRect = !uvRects.empty();
    auto rectCount = rects.size();
    for (size_t i = 0; i < rectCount; ++i) {
      auto& record = rects[i];
      auto& viewMatrix = record->viewMatrix;
      auto& rect = record->rect;
      auto quad = Quad::MakeFrom(rect, &viewMatrix);
      auto& uvRect = hasUVRect ? *uvRects[i] : rect;
      auto uvQuad = Quad::MakeFrom(uvRect);
      auto subset = uvRect;
      if (needSubset) {
        ApplySubsetMode(static_cast<UVSubsetMode>(bitFields.subsetMode), &subset);
      }
      for (size_t j = 4; j >= 1; --j) {
        vertices[index++] = quad.point(j - 1).x;
        vertices[index++] = quad.point(j - 1).y;
        if (bitFields.hasUVCoord) {
          vertices[index++] = uvQuad.point(j - 1).x;
          vertices[index++] = uvQuad.point(j - 1).y;
        }
        if (bitFields.hasColor) {
          WriteUByte4Color(vertices, index, record->color);
        }
        if (needSubset) {
          WriteSubset(vertices, index, subset);
        }
      }
    }
  }
};

PlacementPtr<RectsVertexProvider> RectsVertexProvider::MakeFrom(BlockBuffer* buffer,
                                                                const Rect& rect, AAType aaType) {
  if (rect.isEmpty()) {
    return nullptr;
  }
  auto record = buffer->make<RectRecord>(rect, Matrix::I());
  auto rects = buffer->makeArray<RectRecord>(&record, 1);
  auto uvRects = buffer->makeArray<Rect>(0);
  if (aaType == AAType::Coverage) {
    return buffer->make<AARectsVertexProvider>(std::move(rects), std::move(uvRects), aaType, false,
                                               false, UVSubsetMode::None, buffer->addReference());
  }
  return buffer->make<NonAARectsVertexProvider>(std::move(rects), std::move(uvRects), aaType, false,
                                                false, UVSubsetMode::None, buffer->addReference());
}

PlacementPtr<RectsVertexProvider> RectsVertexProvider::MakeFrom(
    BlockBuffer* buffer, std::vector<PlacementPtr<RectRecord>>&& rects,
    std::vector<PlacementPtr<Rect>>&& uvRects, AAType aaType, bool needUVCoord,
    UVSubsetMode subsetMode) {
  if (rects.empty()) {
    return nullptr;
  }
  bool hasColor = false;
  if (rects.size() > 1) {
    auto& firstColor = rects.front()->color;
    for (auto& record : rects) {
      if (record->color != firstColor) {
        hasColor = true;
        break;
      }
    }
  }
  if (aaType == AAType::Coverage) {
    return buffer->make<AARectsVertexProvider>(
        buffer->makeArray(std::move(rects)), buffer->makeArray(std::move(uvRects)), aaType,
        needUVCoord, hasColor, subsetMode, buffer->addReference());
  }
  return buffer->make<NonAARectsVertexProvider>(
      buffer->makeArray(std::move(rects)), buffer->makeArray(std::move(uvRects)), aaType,
      needUVCoord, hasColor, subsetMode, buffer->addReference());
}

RectsVertexProvider::RectsVertexProvider(PlacementArray<RectRecord>&& rects,
                                         PlacementArray<Rect>&& uvRects, AAType aaType,
                                         bool hasUVCoord, bool hasColor, UVSubsetMode subsetMode,
                                         std::shared_ptr<BlockBuffer> reference)
    : VertexProvider(std::move(reference)), rects(std::move(rects)), uvRects(std::move(uvRects)) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.hasUVCoord = hasUVCoord;
  bitFields.hasColor = hasColor;
  bitFields.subsetMode = static_cast<uint8_t>(subsetMode);
}
}  // namespace tgfx
