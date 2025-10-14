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
#include "tgfx/core/Stroke.h"

namespace tgfx {
inline void WriteUByte4Color(float* vertices, size_t& index, const Color& color) {
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

inline void WriteSubset(float* vertices, size_t& index, const Rect& subset) {
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
    size_t index = 0;
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
    size_t index = 0;
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

// AAStrokeRectVertexProvider currently only supports Miter and Bevel joins.
class AAStrokeRectsVertexProvider final : public RectsVertexProvider {
  PlacementArray<Stroke> strokes = {};

 public:
  AAStrokeRectsVertexProvider(PlacementArray<RectRecord>&& rects, PlacementArray<Rect>&& uvRects,
                              PlacementArray<Stroke>&& strokes, AAType aaType, bool hasUVCoord,
                              bool hasColor, std::shared_ptr<BlockBuffer> reference)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            UVSubsetMode::None, std::move(reference)),
        strokes(std::move(strokes)) {
    if (!this->strokes.empty()) {
      bitFields.hasStroke = true;
      bitFields.lineJoin = static_cast<uint8_t>(this->strokes.front()->join);
    } else {
      bitFields.hasStroke = false;
    }
  }

  void writeQuad(float* vertices, size_t& index, const Quad& quad, const Color& color,
                 float coverage) const {
    for (size_t i = 0; i < 4; ++i) {
      vertices[index++] = quad.point(i).x;
      vertices[index++] = quad.point(i).y;
      vertices[index++] = coverage;
      if (bitFields.hasUVCoord) {
        vertices[index++] = quad.point(i).x;
        vertices[index++] = quad.point(i).y;
      }
      if (bitFields.hasColor) {
        WriteUByte4Color(vertices, index, color);
      }
    }
  }

  size_t vertexCount() const override {
    size_t perVertexCount = (4 + (lineJoin() == LineJoin::Miter ? 4 : 8)) * 2;  // inner + outer
    size_t perVertexDataSize = 3; // x, y, coverage
    if (bitFields.hasUVCoord) {
      perVertexDataSize += 2;
    }
    if (bitFields.hasColor) {
      perVertexDataSize += 1;
    }
    return rects.size() * perVertexCount * perVertexDataSize;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    for (size_t i = 0; i < rects.size(); ++i) {
      const auto& stroke = strokes[i];
      const auto& record = rects[i];
      auto& viewMatrix = record->viewMatrix;
      Point strokeSize = {stroke->width, stroke->width};
      if (stroke->width > 0.f) {
        strokeSize.x =
            std::abs(strokeSize.x * viewMatrix.getScaleX() + strokeSize.y * viewMatrix.getSkewX());
        strokeSize.y =
            std::abs(strokeSize.x * viewMatrix.getSkewY() + strokeSize.y * viewMatrix.getScaleY());
      } else {
        strokeSize.set(1.0f, 1.0f);
      }
      const float dx = strokeSize.x;
      const float dy = strokeSize.y;
      const float rx = dx * 0.5f;
      const float ry = dy * 0.5f;
      auto rect = viewMatrix.mapRect(record->rect);  // to device space
      auto outSide = rect.makeOutset(rx, ry);
      auto outSideAssist = rect;
      auto inSide = rect.makeInset(rx, ry);

      // If we have a degenerate stroking rect(ie the stroke is larger than inner rect) then we
      // make a degenerate inside rect to avoid double hitting.  We will also jam all the points
      // together when we render these rects.
      const auto spare = std::min(rect.width() - dx, rect.height() - dy);
      const auto isDegenerate = spare <= 0.0f;
      if (isDegenerate) {
        inSide.left = inSide.right = rect.centerX();
        inSide.top = inSide.bottom = rect.centerY();
      }
      // For bevel-stroke, use 2 Rect instances(outside and outsideAssist)
      // to draw the outside of the octagon. Because there are 8 vertices on the outer
      // edge, while vertex number of inner edge is 4, the same as miter-stroke.
      const auto isBevelJoin = lineJoin() == LineJoin::Bevel;
      if (isBevelJoin) {
        outSide.inset(0, ry);
        outSideAssist.outset(0, ry);
      }

      // How much do we inset toward the inside of the strokes?
      const float inset = std::min(0.5f, std::min(rx, ry));
      float innerCoverage = 1.0f;
      if (inset < 0.5f) {
        // Stroke is subpixel, so reduce the coverage to simulate the narrower strokes.
        innerCoverage = 2.0f * inset / (inset + 0.5f);
      }

      // How much do we outset away from the outside of the strokes?
      // We always want to keep the AA picture frame one pixel wide.
      const float outset = 1.0f - inset;
      const float outerCoverage = 0.0f;

      // How much do we outset away from the interior side of the stroke (toward the center)?
      const float interiorOutset = outset;
      const float interiorCoverage = outerCoverage;

      writeQuad(vertices, index, Quad::MakeFrom(outSide.makeInset(-outset, -outset)), record->color,
                outerCoverage);
      if (isBevelJoin) {
        writeQuad(vertices, index, Quad::MakeFrom(outSideAssist.makeInset(-outset, -outset)),
                  record->color, outerCoverage);
      }
      writeQuad(vertices, index, Quad::MakeFrom(outSide.makeInset(inset, inset)), record->color,
                innerCoverage);
      if (isBevelJoin) {
        writeQuad(vertices, index, Quad::MakeFrom(outSideAssist.makeInset(inset, inset)),
                  record->color, innerCoverage);
      }
      if (!isDegenerate) {
        // Interior inset rect (toward stroke).
        writeQuad(vertices, index, Quad::MakeFrom(inSide.makeInset(-inset, -inset)), record->color,
                  innerCoverage);
        // Interior outset rect (away from stroke, toward center of rect).
        Rect interiorAABoundary = inSide.makeInset(interiorOutset, interiorOutset);
        float coverageBackset = 0;  // Adds back coverage when the interior AA edges cross.
        if (interiorAABoundary.left > interiorAABoundary.right) {
          coverageBackset =
              (interiorAABoundary.left - interiorAABoundary.right) / (interiorOutset * 2);
          interiorAABoundary.left = interiorAABoundary.right = interiorAABoundary.centerX();
        }
        if (interiorAABoundary.top > interiorAABoundary.bottom) {
          coverageBackset =
              std::max((interiorAABoundary.top - interiorAABoundary.bottom) / (interiorOutset * 2),
                       coverageBackset);
          interiorAABoundary.top = interiorAABoundary.bottom = interiorAABoundary.centerY();
        }
        if (coverageBackset > 0) {
          // The interior edges crossed. Lerp back toward innerCoverage, which is what this op
          // will draw in the degenerate case. This gives a smooth transition into the degenerate
          // case.
          innerCoverage += innerCoverage * (1 - coverageBackset) + innerCoverage * coverageBackset;
        }
        writeQuad(vertices, index, Quad::MakeFrom(interiorAABoundary), record->color,
                  interiorCoverage);
      } else {
        // When the interior rect has become degenerate we smoosh to a single point
        writeQuad(vertices, index, Quad::MakeFrom(inSide), record->color, innerCoverage);
        writeQuad(vertices, index, Quad::MakeFrom(inSide), record->color, innerCoverage);
      }
    }
  }
};

class NonAAStrokeRectsVertexProvider final : public RectsVertexProvider {
  PlacementArray<Stroke> strokes = {};

 public:
  NonAAStrokeRectsVertexProvider(PlacementArray<RectRecord>&& rects, PlacementArray<Rect>&& uvRects,
                                 PlacementArray<Stroke>&& strokes, AAType aaType, bool hasUVCoord,
                                 bool hasColor, std::shared_ptr<BlockBuffer> reference)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            UVSubsetMode::None, std::move(reference)),
        strokes(std::move(strokes)) {
    if (!this->strokes.empty()) {
      bitFields.hasStroke = true;
      bitFields.lineJoin = static_cast<uint8_t>(this->strokes.front()->join);
    } else {
      bitFields.hasStroke = false;
    }
  }

  void writeQuad(float* vertices, size_t& index, const Quad& quad, const Color& color) const {
    for (size_t i = 0; i < 4; ++i) {
      vertices[index++] = quad.point(i).x;
      vertices[index++] = quad.point(i).y;
      if (bitFields.hasUVCoord) {
        vertices[index++] = quad.point(i).x;
        vertices[index++] = quad.point(i).y;
      }
      if (bitFields.hasColor) {
        WriteUByte4Color(vertices, index, color);
      }
    }
  }

  size_t vertexCount() const override {
    size_t perVertexCount = (4 + (lineJoin() == LineJoin::Miter ? 4 : 8));  // outer edge only
    size_t perVertexDataSize = 2;// x, y
    if (bitFields.hasUVCoord) {
      perVertexDataSize += 2;
    }
    if (bitFields.hasColor) {
      perVertexDataSize += 1;
    }
    return rects.size() * perVertexCount * perVertexDataSize;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    for (size_t i = 0; i < rects.size(); ++i) {
      const auto& stroke = strokes[i];
      const auto& record = rects[i];
      auto& viewMatrix = record->viewMatrix;
      Point strokeSize = {stroke->width, stroke->width};
      if (stroke->width > 0.f) {
        strokeSize.x =
            std::abs(strokeSize.x * viewMatrix.getScaleX() + strokeSize.y * viewMatrix.getSkewX());
        strokeSize.y =
            std::abs(strokeSize.x * viewMatrix.getSkewY() + strokeSize.y * viewMatrix.getScaleY());
      } else {
        strokeSize.set(1.0f, 1.0f);
      }
      const float dx = strokeSize.x;
      const float dy = strokeSize.y;
      const float rx = strokeSize.x * 0.5f;
      const float ry = strokeSize.y * 0.5f;
      auto rect = viewMatrix.mapRect(record->rect);  // to device space
      auto outSide = rect.makeOutset(rx, ry);
      auto outSideAssist = rect;
      auto inSide = rect.makeInset(rx, ry);
      const auto isBevelJoin = lineJoin() == LineJoin::Bevel;

      // If we have a degenerate stroking rect(ie the stroke is larger than inner rect) then we
      // make a degenerate inside rect to avoid double hitting.  We will also jam all the points
      // together when we render these rects.
      if (auto spare = std::min(rect.width() - dx, rect.height() - dy); spare <= 0.0f) {
        // process degenerate rect
        inSide.left = inSide.right = rect.centerX();
        inSide.top = inSide.bottom = rect.centerY();
      }
      // For bevel-stroke, use 2 Rect instances(outside and outsideAssist)
      // to draw the outside of the octagon. Because there are 8 vertices on the outer
      // edge, while vertex number of inner edge is 4, the same as miter-stroke.
      if (isBevelJoin) {
        outSide.inset(0, ry);
        outSideAssist.outset(0, ry);
      }

      writeQuad(vertices, index, Quad::MakeFrom(outSide), record->color);
      if (isBevelJoin) {
        writeQuad(vertices, index, Quad::MakeFrom(outSideAssist), record->color);
      }
      writeQuad(vertices, index, Quad::MakeFrom(inSide), record->color);
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
    UVSubsetMode subsetMode, std::vector<PlacementPtr<Stroke>>&& strokes) {
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
  auto rectArray = buffer->makeArray(std::move(rects));
  auto uvRectArray = buffer->makeArray(std::move(uvRects));
  if (strokes.empty()) {
    if (aaType == AAType::Coverage) {
      return buffer->make<AARectsVertexProvider>(std::move(rectArray), std::move(uvRectArray),
                                                 aaType, needUVCoord, hasColor, subsetMode,
                                                 buffer->addReference());
    }
    return buffer->make<NonAARectsVertexProvider>(std::move(rectArray), std::move(uvRectArray),
                                                  aaType, needUVCoord, hasColor, subsetMode,
                                                  buffer->addReference());
  }

  auto strokeArray = buffer->makeArray(std::move(strokes));
  if (aaType == AAType::Coverage) {
    return buffer->make<AAStrokeRectsVertexProvider>(std::move(rectArray), std::move(uvRectArray),
                                                     std::move(strokeArray), aaType, needUVCoord,
                                                     hasColor, buffer->addReference());
  }
  return buffer->make<NonAAStrokeRectsVertexProvider>(std::move(rectArray), std::move(uvRectArray),
                                                      std::move(strokeArray), aaType, needUVCoord,
                                                      hasColor, buffer->addReference());
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
