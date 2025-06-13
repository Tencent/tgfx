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
#include "gpu/Quad.h"

namespace tgfx {

// Inset texture coordinates by 0.5 pixels inward to avoid drawImageRect sampling outside the Rect.
static constexpr float offset = 0.5f;

static void WriteUByte4Color(float* vertices, int& index, const Color& color) {
  auto bytes = reinterpret_cast<uint8_t*>(&vertices[index++]);
  bytes[0] = static_cast<uint8_t>(color.red * 255);
  bytes[1] = static_cast<uint8_t>(color.green * 255);
  bytes[2] = static_cast<uint8_t>(color.blue * 255);
  bytes[3] = static_cast<uint8_t>(color.alpha * 255);
}

class AARectsVertexProvider : public RectsVertexProvider {
 public:
  AARectsVertexProvider(PlacementArray<RectRecord>&& rects, AAType aaType, bool hasUVCoord,
                        bool hasColor, SrcRectConstraint constraint, SamplingOptions options)
      : RectsVertexProvider(std::move(rects), aaType, hasUVCoord, hasColor, constraint, options){
  }

  size_t vertexCount() const override {
    size_t perVertexCount = bitFields.hasUVCoord ? 5 : 3;
    if (bitFields.hasColor) {
      perVertexCount += 1;
    }
    if(srcRectConstraint == SrcRectConstraint::Strict_SrcRectConstraint) {
      perVertexCount += 4;
    }
    return rects.size() * 2 * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    for (auto& record : rects) {
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
      auto uvInsetQuad = Quad::MakeFrom(insetBounds);
      auto uvOutsetQuad = Quad::MakeFrom(outsetBounds);
      Rect subSet;
      bool needSubset = getInsetRect(rect, &subSet);
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
          if(needSubset) {
            vertices[index++] = subSet.left;
            vertices[index++] = subSet.top;
            vertices[index++] = subSet.right;
            vertices[index++] = subSet.bottom;
          }
        }
      }
    }
  }
};

class NonAARectVertexProvider : public RectsVertexProvider {
 public:
  NonAARectVertexProvider(PlacementArray<RectRecord>&& rects, AAType aaType, bool hasUVCoord,
                          bool hasColor, SrcRectConstraint constraint, SamplingOptions options)
      : RectsVertexProvider(std::move(rects), aaType, hasUVCoord, hasColor,  constraint, options) {
  }

  size_t vertexCount() const override {
    size_t perVertexCount = bitFields.hasUVCoord ? 4 : 2;
    if (bitFields.hasColor) {
      perVertexCount += 1;
    }
    if(srcRectConstraint == SrcRectConstraint::Strict_SrcRectConstraint) {
      perVertexCount += 4;
    }
    return rects.size() * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    for (auto& record : rects) {
      auto& viewMatrix = record->viewMatrix;
      auto& rect = record->rect;
      auto quad = Quad::MakeFrom(rect, &viewMatrix);
      auto uvQuad = Quad::MakeFrom(rect);
      Rect subSet;
      bool needSubset = getInsetRect(rect, &subSet);
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
        if(needSubset) {
          vertices[index++] = subSet.left;
          vertices[index++] = subSet.top;
          vertices[index++] = subSet.right;
          vertices[index++] = subSet.bottom;
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
  if (aaType == AAType::Coverage) {
    return buffer->make<AARectsVertexProvider>(std::move(rects), aaType, false, false, SrcRectConstraint::Fast_SrcRectConstraint, SamplingOptions());
  }
  return buffer->make<NonAARectVertexProvider>(std::move(rects), aaType, false, false, SrcRectConstraint::Fast_SrcRectConstraint, SamplingOptions());
}

PlacementPtr<RectsVertexProvider> RectsVertexProvider::MakeFrom(
    BlockBuffer* buffer, std::vector<PlacementPtr<RectRecord>>&& rects, AAType aaType,
    bool needUVCoord, SrcRectConstraint constraint, SamplingOptions samplingOpt) {
  if (rects.empty()) {
    return nullptr;
  }
  auto hasColor = false;
  auto hasUVCoord = false;
  if (rects.size() > 1) {
    auto& firstColor = rects.front()->color;
    for (auto& record : rects) {
      if (record->color != firstColor) {
        hasColor = true;
        break;
      }
    }
    if (needUVCoord) {
      auto& firstMatrix = rects.front()->viewMatrix;
      for (auto& record : rects) {
        if (record->viewMatrix != firstMatrix) {
          hasUVCoord = true;
          break;
        }
      }
    }
  }
  auto array = buffer->makeArray(std::move(rects));
  if (aaType == AAType::Coverage) {
    return buffer->make<AARectsVertexProvider>(std::move(array), aaType, hasUVCoord, hasColor, constraint, samplingOpt);
  }
  return buffer->make<NonAARectVertexProvider>(std::move(array), aaType, hasUVCoord, hasColor, constraint, samplingOpt);
}

RectsVertexProvider::RectsVertexProvider(PlacementArray<RectRecord>&& rects, AAType aaType,
                                         bool hasUVCoord, bool hasColor, SrcRectConstraint constraint, SamplingOptions samplingOpt)
    : rects(std::move(rects)), srcRectConstraint(constraint), samplingOptions(samplingOpt) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.hasUVCoord = hasUVCoord;
  bitFields.hasColor = hasColor;
}

bool RectsVertexProvider::getInsetRect(const Rect& srcRect, Rect* dstRect) const {
  DEBUG_ASSERT(dstRect)
  Rect result = srcRect;
  if(srcRectConstraint == SrcRectConstraint::Strict_SrcRectConstraint) {
    if(samplingOptions.filterMode == FilterMode::Nearest) {
      // Make sure our insetting puts us at pixel centers.
      result.roundOut();
    }
    result.inset(offset, offset);
    *dstRect = result;
    return true;
  }
  return false;
}

}  // namespace tgfx
