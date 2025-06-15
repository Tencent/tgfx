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
  AARectsVertexProvider(PlacementArray<RectRecord>&& rects, AAType aaType, bool hasUVCoord,
                        bool hasColor, PlacementArray<Stroke>&& strokes)
      : RectsVertexProvider(std::move(rects), aaType, hasUVCoord, hasColor, std::move(strokes)) {
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
        }
      }
    }

    auto vertextCount = vertexCount();
    printf("--vertextData--vertexCount: %zu\n", vertextCount);
    for (size_t i = 0; i < vertextCount; ++i) {
      printf("%f, ", vertices[i]);
      if ((i + 1) % 10 == 0) {
        printf("\n");
      }
    }
    printf("\n--vertextData end--\n");
  }
};

class NonAARectVertexProvider : public RectsVertexProvider {
 public:
  NonAARectVertexProvider(PlacementArray<RectRecord>&& rects, AAType aaType, bool hasUVCoord,
                          bool hasColor, PlacementArray<Stroke>&& strokes)
      : RectsVertexProvider(std::move(rects), aaType, hasUVCoord, hasColor, std::move(strokes)) {
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
    for (auto& record : rects) {
      auto& viewMatrix = record->viewMatrix;
      auto& rect = record->rect;
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
          WriteUByte4Color(vertices, index, record->color);
        }
      }
    }
    auto vertextCount = vertexCount();
    printf("-NonAARectVertexProvider-vertextData--vertexCount: %zu\n", vertextCount);
    for (size_t i = 0; i < vertextCount; ++i) {
      printf("%f, ", vertices[i]);
      if ((i + 1) % 3 == 0) {
        printf("\n");
      }
    }
    printf("\n--vertextData end--\n");
  }
};

// AAStrokeRectsVertexProvider only supports Miter and Bevel stroke types.
class AAStrokeRectsVertexProvider : public RectsVertexProvider {
 public:
  AAStrokeRectsVertexProvider(PlacementArray<RectRecord>&& rects, AAType aaType, bool hasUVCoord,
                              bool hasColor, PlacementArray<Stroke>&& strokes)
      : RectsVertexProvider(std::move(rects), aaType, hasUVCoord, hasColor, std::move(strokes)) {
  }

  size_t vertexCount() const override {
    size_t perVertexCount = (4 + (lineJoin() == LineJoin::Miter ? 4 : 8)) * 2;  // inner + outer
    size_t perVertexDataSize = 3;                         // x, y, coverage
    if (bitFields.hasColor) {
      perVertexDataSize += 1;
    }
    return rects.size() * perVertexCount * perVertexDataSize;
  }

  void getVertices(float* vertices) const override {
    int index = 0;
    auto count = rects.size();
    for (size_t i = 0; i < count; ++i) {
      auto stroke = strokes[i].get();
      auto record = rects[i].get();
      auto& viewMatrix = record->viewMatrix;
      Point strokeSize = {stroke->width, stroke->width};
      if (stroke->width > 0) {
        viewMatrix.mapPoints(&strokeSize, 1);
        strokeSize.x = std::abs(strokeSize.x);
        strokeSize.y = std::abs(strokeSize.y);
      } else {
        strokeSize.set(1.0f, 1.0f);
      }
      const float dx = strokeSize.x;
      const float dy = strokeSize.y;
      strokeSize.x *= 0.5f;
      strokeSize.y *= 0.5f;
      auto& rect = record->rect;
      auto outSide = rect.makeOutset(strokeSize.x, strokeSize.y);
      auto outSideAssist = rect;
      auto inSide = rect.makeInset(strokeSize.x, strokeSize.y);

      // If we have a degenerate stroking rect(ie the stroke is larger than inner rect) then we
      // make a degenerate inside rect to avoid double hitting.  We will also jam all of the points
      // together when we render these rects.
      float spare = std::min(rect.width() - dx, rect.height() - dy);
      bool isDegenerate = spare <= 0;
      if (isDegenerate) {
        inSide.left = inSide.right = rect.centerX();
        inSide.top = inSide.bottom = rect.centerY();
      }

      // For bevel-stroke, use 2 SkRect instances(devOutside and devOutsideAssist)
      // to draw the outside of the octagon. Because there are 8 vertices on the outer
      // edge, while vertex number of inner edge is 4, the same as miter-stroke.
      if (lineJoin() != LineJoin::Miter) {
        outSide.inset(0, strokeSize.y);
        outSideAssist.outset(0, strokeSize.y);
      }

      // How much do we inset toward the inside of the strokes?
      float inset = std::min(0.5f, std::min(strokeSize.x, strokeSize.y));
      float innerCoverage = 1;
      if (inset < 0.5f) {
        // Stroke is subpixel, so reduce the coverage to simulate the narrower strokes.
        innerCoverage = 2 * inset / (inset + .5f);
      }

      // How much do we outset away from the outside of the strokes?
      // We always want to keep the AA picture frame one pixel wide.
      float outset = 1 - inset;
      float outerCoverage = 0;

      // How much do we outset away from the interior side of the stroke (toward the center)?
      float interiorOutset = outset;
      float interiorCoverage = outerCoverage;

      writeQuad(vertices, index, Quad::MakeFrom(outSide.makeInset(-outset, -outset)), record->color,
                outerCoverage);
      if (lineJoin() != LineJoin::Miter) {
        writeQuad(vertices, index, Quad::MakeFrom(outSideAssist.makeInset(-outset, -outset)),
                  record->color, outerCoverage);
      }
      writeQuad(vertices, index, Quad::MakeFrom(outSide.makeInset(inset, inset)), record->color,
                innerCoverage);
      if (lineJoin() != LineJoin::Miter) {
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

    auto vertextCount = vertexCount();
    printf("--vertextData--vertexCount: %zu\n", vertextCount);
    for (size_t i = 0; i < vertextCount; ++i) {
      printf("%f, ", vertices[i]);
      if ((i + 1) % 3 == 0) {
        printf("\n");
      }
    }
    printf("\n--vertextData-end-\n");
  }
};

PlacementPtr<RectsVertexProvider> RectsVertexProvider::MakeFrom(BlockBuffer* buffer,
                                                                const Rect& rect, AAType aaType) {
  if (rect.isEmpty()) {
    return nullptr;
  }
  auto record = buffer->make<RectRecord>(rect, Matrix::I());
  auto rects = buffer->makeArray<RectRecord>(&record, 1);
  PlacementArray<Stroke> strokes = {};
  if (aaType == AAType::Coverage) {
    return buffer->make<AARectsVertexProvider>(std::move(rects), aaType, false, false,
                                               std::move(strokes));
  }
  return buffer->make<NonAARectVertexProvider>(std::move(rects), aaType, false, false,
                                               std::move(strokes));
}

PlacementPtr<RectsVertexProvider> RectsVertexProvider::MakeFrom(
    BlockBuffer* buffer, std::vector<PlacementPtr<RectRecord>>&& rects, AAType aaType,
    bool needUVCoord, std::vector<PlacementPtr<Stroke>>&& strokes) {
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
  auto strokeArray = buffer->makeArray(std::move(strokes));
  if (aaType == AAType::Coverage) {
    if (strokeArray.empty()) {
      return buffer->make<AARectsVertexProvider>(std::move(array), aaType, hasUVCoord, hasColor,
                                                 std::move(strokeArray));
    } else {
      return buffer->make<AAStrokeRectsVertexProvider>(std::move(array), aaType, hasUVCoord, hasColor,std::move(strokeArray));
    }
  }
  return buffer->make<NonAARectVertexProvider>(std::move(array), aaType, hasUVCoord, hasColor,
                                               std::move(strokeArray));
}

RectsVertexProvider::RectsVertexProvider(PlacementArray<RectRecord>&& rects, AAType aaType,
                                         bool hasUVCoord, bool hasColor,
                                         PlacementArray<Stroke>&& strokes)
    : rects(std::move(rects)), strokes(std::move(strokes)) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.hasUVCoord = hasUVCoord;
  bitFields.hasColor = hasColor;
  if (!this->strokes.empty()) {
    bitFields.hasStroke = true;
    bitFields.lineJoin = static_cast<uint8_t>(this->strokes.front().get()->join);
  } else {
    bitFields.hasStroke = false;
  }
}

void RectsVertexProvider::writeQuad(float* vertices, int& index, Quad quad, Color color,
                                    float coverage) const {
  for (size_t i = 0; i < 4; ++i) {
    vertices[index++] = quad.point(i).x;
    vertices[index++] = quad.point(i).y;
    if (coverage >= 0) {
      vertices[index++] = coverage;
    }
    if (bitFields.hasUVCoord) {
      vertices[index++] = quad.point(i).x;
      vertices[index++] = quad.point(i).y;
    }
    if (bitFields.hasColor) {
      WriteUByte4Color(vertices, index, color);
    }
  }
}
}  // namespace tgfx
