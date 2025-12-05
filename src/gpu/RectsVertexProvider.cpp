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
#include <array>
#include "core/ColorSpaceXformSteps.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/MathExtra.h"
#include "gpu/Quad.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
inline float ToUByte4PMColor(const Color& color, const ColorSpaceXformSteps* steps) {
  PMColor pmColor = color.premultiply();
  if (steps) {
    steps->apply(pmColor.array());
  }
  float compressedColor = 0.0f;
  auto bytes = reinterpret_cast<uint8_t*>(&compressedColor);
  bytes[0] = static_cast<uint8_t>(pmColor.red * 255);
  bytes[1] = static_cast<uint8_t>(pmColor.green * 255);
  bytes[2] = static_cast<uint8_t>(pmColor.blue * 255);
  bytes[3] = static_cast<uint8_t>(pmColor.alpha * 255);
  return compressedColor;
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
                        std::shared_ptr<BlockAllocator> reference,
                        std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            subsetMode, std::move(reference), std::move(colorSpace)) {
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
    std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
    if (bitFields.hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
      steps =
          std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                                 _dstColorSpace.get(), AlphaType::Premultiplied);
    }
    for (size_t i = 0; i < rectCount; ++i) {
      auto& record = rects[i];
      auto& viewMatrix = record->viewMatrix;
      auto& rect = record->rect;
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        compressedColor = ToUByte4PMColor(record->color, steps.get());
      }

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
            vertices[index++] = compressedColor;
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
                           std::shared_ptr<BlockAllocator> reference,
                           std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            subsetMode, std::move(reference), std::move(colorSpace)) {
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
    std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
    if (bitFields.hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
      steps =
          std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                                 _dstColorSpace.get(), AlphaType::Premultiplied);
    }
    for (size_t i = 0; i < rectCount; ++i) {
      auto& record = rects[i];
      auto& viewMatrix = record->viewMatrix;
      auto& rect = record->rect;
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        compressedColor = ToUByte4PMColor(record->color, steps.get());
      }
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
          vertices[index++] = compressedColor;
        }
        if (needSubset) {
          WriteSubset(vertices, index, subset);
        }
      }
    }
  }
};

// Anti-aliased angular stroke joins: Miter and Bevel.
class AAAngularStrokeRectsVertexProvider final : public RectsVertexProvider {
  PlacementArray<Stroke> strokes = {};

 public:
  AAAngularStrokeRectsVertexProvider(PlacementArray<RectRecord>&& rects,
                                     PlacementArray<Rect>&& uvRects,
                                     PlacementArray<Stroke>&& strokes, AAType aaType,
                                     bool hasUVCoord, bool hasColor,
                                     std::shared_ptr<BlockAllocator> reference,
                                     std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            UVSubsetMode::None, std::move(reference), std::move(colorSpace)),
        strokes(std::move(strokes)) {
    DEBUG_ASSERT(!this->strokes.empty() && this->strokes.size() == this->rects.size());
    _lineJoin = this->strokes.front()->join;
  }

  void writeQuad(float* vertices, size_t& index, const Quad& quad, const Quad& uvQuad,
                 float compressedColor, float coverage) const {
    for (size_t i = 0; i < 4; ++i) {
      vertices[index++] = quad.point(i).x;
      vertices[index++] = quad.point(i).y;
      vertices[index++] = coverage;
      if (bitFields.hasUVCoord) {
        vertices[index++] = uvQuad.point(i).x;
        vertices[index++] = uvQuad.point(i).y;
      }
      if (bitFields.hasColor) {
        vertices[index++] = compressedColor;
      }
    }
  }

  size_t vertexCount() const override {
    size_t perVertexCount = (lineJoin() == LineJoin::Miter ? 8 : 12) * 2;  // inner + outer
    size_t perVertexDataSize = 3;                                          // x, y ,coverage
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
    const auto isBevelJoin = lineJoin() == LineJoin::Bevel;
    const auto hasUVCoord = bitFields.hasUVCoord;
    std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
    if (bitFields.hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
      steps =
          std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                                 _dstColorSpace.get(), AlphaType::Premultiplied);
    }
    for (size_t i = 0; i < rects.size(); ++i) {
      const auto& stroke = strokes[i];
      const auto& record = rects[i];
      auto& viewMatrix = record->viewMatrix;
      const auto scale = std::sqrt(viewMatrix.getScaleX() * viewMatrix.getScaleX() +
                                   viewMatrix.getSkewY() * viewMatrix.getSkewY());
      // we want the new edge to be 0.5px away from the old line.
      const auto padding = 0.5f / scale;
      auto strokeWidth = stroke->width > 0.0f ? stroke->width : 1.0f / scale;
      const auto halfWidth = strokeWidth * 0.5f;
      auto& rect = record->rect;
      auto outSide = rect.makeOutset(halfWidth, halfWidth);
      auto outSideAssist = rect;
      auto inSide = rect.makeInset(halfWidth, halfWidth);
      Quad uvQuad = Quad::MakeFrom({});
      auto outUV = outSide;
      auto inUV = inSide;
      auto assistUV = outSideAssist;
      float vOffset = 0.0f;
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        compressedColor = ToUByte4PMColor(record->color, steps.get());
      }
      if (hasUVCoord) {
        auto& uvRect = *uvRects[i];
        auto uOffset = halfWidth / rect.width() * uvRect.width();
        vOffset = halfWidth / rect.height() * uvRect.height();
        outUV = uvRect.makeOutset(uOffset, vOffset);
        inUV = uvRect.makeInset(uOffset, vOffset);
        assistUV = uvRect;
      }
      // If we have a degenerate stroking rect(ie the stroke is larger than inner rect) then we
      // make a degenerate inside rect to avoid double hitting.  We will also jam all the points
      // together when we render these rects.
      const auto isDegenerate = inSide.isEmpty();
      if (isDegenerate) {
        inSide.left = inSide.right = rect.centerX();
        inSide.top = inSide.bottom = rect.centerY();
        if (hasUVCoord) {
          inUV.left = inUV.right = uvRects[i]->centerX();
          inUV.top = inUV.bottom = uvRects[i]->centerY();
        }
      }
      // For bevel-stroke, use 2 Rect instances(outside and outsideAssist)
      // to draw the outside of the octagon. Because there are 8 vertices on the outer
      // edge, while vertex number of inner edge is 4, the same as miter-stroke.
      if (isBevelJoin) {
        outSide.inset(0, halfWidth);
        outSideAssist.outset(0, halfWidth);
        if (hasUVCoord) {
          outUV.inset(0.0f, vOffset);
          assistUV.outset(0.0f, vOffset);
        }
      }

      // How much do we inset toward the inside of the strokes?
      const float inset = std::min(padding, halfWidth);
      auto innerCoverage = 1.0f;
      if (inset < padding) {
        // Stroke is subpixel, so reduce the coverage to simulate the narrower strokes.
        innerCoverage = 2.0f * inset / (inset + padding);
      }

      // How much do we outset away from the outside of the strokes?
      // We always want to keep the AA picture frame one pixel wide.
      const auto outset = 2.0f * padding - inset;
      constexpr float outerCoverage = 0.0f;

      // How much do we outset away from the interior side of the stroke (toward the center)?
      const auto interiorOutset = outset;
      const auto interiorCoverage = outerCoverage;

      // Exterior outset rect (away from stroke).
      const auto outOutsetQuad = Quad::MakeFrom(outSide.makeOutset(outset, outset), &viewMatrix);
      if (hasUVCoord) {
        uvQuad = Quad::MakeFrom(outUV.makeOutset(outset, outset));
      }
      writeQuad(vertices, index, outOutsetQuad, uvQuad, compressedColor, outerCoverage);
      if (isBevelJoin) {
        const auto assistOutsetQuad =
            Quad::MakeFrom(outSideAssist.makeOutset(outset, outset), &viewMatrix);
        if (hasUVCoord) {
          uvQuad = Quad::MakeFrom(assistUV.makeOutset(outset, outset));
        }
        writeQuad(vertices, index, assistOutsetQuad, uvQuad, compressedColor, outerCoverage);
      }
      const auto outInsetQuad = Quad::MakeFrom(outSide.makeInset(inset, inset), &viewMatrix);
      if (hasUVCoord) {
        uvQuad = Quad::MakeFrom(outUV.makeInset(inset, inset));
      }
      writeQuad(vertices, index, outInsetQuad, uvQuad, compressedColor, innerCoverage);
      if (isBevelJoin) {
        const auto assistInsetQuad =
            Quad::MakeFrom(outSideAssist.makeInset(inset, inset), &viewMatrix);
        if (hasUVCoord) {
          uvQuad = Quad::MakeFrom(assistUV.makeInset(inset, inset));
        }
        writeQuad(vertices, index, assistInsetQuad, uvQuad, compressedColor, innerCoverage);
      }
      if (!isDegenerate) {
        // Interior inset rect (toward stroke).
        const auto innerInsetQuad = Quad::MakeFrom(inSide.makeOutset(inset, inset), &viewMatrix);
        if (hasUVCoord) {
          uvQuad = Quad::MakeFrom(inUV.makeOutset(inset, inset));
        }
        writeQuad(vertices, index, innerInsetQuad, uvQuad, compressedColor, innerCoverage);
        // Interior outset rect (away from stroke, toward center of rect).
        Rect interiorAABoundary = inSide.makeInset(interiorOutset, interiorOutset);
        float coverageBackset = 0.0f;  // Adds back coverage when the interior AA edges cross.
        if (interiorAABoundary.left > interiorAABoundary.right) {
          coverageBackset =
              (interiorAABoundary.left - interiorAABoundary.right) / (interiorOutset * 2.0f);
          interiorAABoundary.left = interiorAABoundary.right = interiorAABoundary.centerX();
        }
        if (interiorAABoundary.top > interiorAABoundary.bottom) {
          coverageBackset = std::max(
              (interiorAABoundary.top - interiorAABoundary.bottom) / (interiorOutset * 2.0f),
              coverageBackset);
          interiorAABoundary.top = interiorAABoundary.bottom = interiorAABoundary.centerY();
        }
        if (coverageBackset > 0.0f) {
          // The interior edges crossed. Lerp back toward innerCoverage, which is what this op
          // will draw in the degenerate case. This gives a smooth transition into the degenerate
          // case.
          innerCoverage +=
              interiorCoverage * (1 - coverageBackset) + innerCoverage * coverageBackset;
        }
        const auto innerAAQuad = Quad::MakeFrom(interiorAABoundary, &viewMatrix);
        if (hasUVCoord) {
          auto uvBoundary = inUV.makeInset(interiorOutset, interiorOutset);
          if (uvBoundary.isEmpty()) {
            uvBoundary.left = uvBoundary.right = inUV.centerX();
            uvBoundary.top = uvBoundary.bottom = inUV.centerY();
          }
          uvQuad = Quad::MakeFrom(uvBoundary);
        }
        writeQuad(vertices, index, innerAAQuad, uvQuad, compressedColor, interiorCoverage);
      } else {
        // When the interior rect has become degenerate we smoosh to a single point
        const auto innerQuad = Quad::MakeFrom(inSide, &viewMatrix);
        if (hasUVCoord) {
          uvQuad = Quad::MakeFrom(inUV);
        }
        writeQuad(vertices, index, innerQuad, uvQuad, compressedColor, innerCoverage);
        writeQuad(vertices, index, innerQuad, uvQuad, compressedColor, innerCoverage);
      }
    }
  }
};

// Non anti-aliased angular stroke joins: Miter and Bevel.
class NonAAAngularStrokeRectsVertexProvider final : public RectsVertexProvider {
  PlacementArray<Stroke> strokes = {};

 public:
  NonAAAngularStrokeRectsVertexProvider(PlacementArray<RectRecord>&& rects,
                                        PlacementArray<Rect>&& uvRects,
                                        PlacementArray<Stroke>&& strokes, AAType aaType,
                                        bool hasUVCoord, bool hasColor,
                                        std::shared_ptr<BlockAllocator> reference,
                                        std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            UVSubsetMode::None, std::move(reference), std::move(colorSpace)),
        strokes(std::move(strokes)) {
    DEBUG_ASSERT(!this->strokes.empty() && this->strokes.size() == this->rects.size());
    _lineJoin = this->strokes.front()->join;
  }

  void writeQuad(float* vertices, size_t& index, const Quad& quad, const Quad& uvQuad,
                 float compressedColor) const {
    for (size_t i = 0; i < 4; ++i) {
      vertices[index++] = quad.point(i).x;
      vertices[index++] = quad.point(i).y;
      if (bitFields.hasUVCoord) {
        vertices[index++] = uvQuad.point(i).x;
        vertices[index++] = uvQuad.point(i).y;
      }
      if (bitFields.hasColor) {
        vertices[index++] = compressedColor;
      }
    }
  }

  size_t vertexCount() const override {
    size_t perVertexCount = lineJoin() == LineJoin::Miter ? 8 : 12;  // outer edge only
    size_t perVertexDataSize = 2;                                    // x, y
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
    const auto hasUVCoord = bitFields.hasUVCoord;
    std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
    if (bitFields.hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
      steps =
          std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                                 _dstColorSpace.get(), AlphaType::Premultiplied);
    }
    for (size_t i = 0; i < rects.size(); ++i) {
      const auto& stroke = strokes[i];
      const auto& record = rects[i];
      auto& viewMatrix = record->viewMatrix;

      // we want the new edge to be 0.5px away from the old line.
      auto strokeWidth = stroke->width;
      if (strokeWidth < 0.0f) {
        const auto scale = std::sqrt(viewMatrix.getScaleX() * viewMatrix.getScaleX() +
                                     viewMatrix.getSkewY() * viewMatrix.getSkewY());
        strokeWidth = 1.0f / scale;
      }
      const auto halfWidth = strokeWidth * 0.5f;
      auto rect = record->rect;
      auto outSide = rect.makeOutset(halfWidth, halfWidth);
      auto outSideAssist = rect;
      auto inSide = rect.makeInset(halfWidth, halfWidth);
      Quad uvQuad = Quad::MakeFrom({});
      auto outUV = outSide;
      auto inUV = inSide;
      auto assistUV = outSideAssist;
      auto vOffset = 0.0f;
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        compressedColor = ToUByte4PMColor(record->color, steps.get());
      }
      if (hasUVCoord) {
        auto& uvRect = *uvRects[i];
        const auto uOffset = halfWidth / rect.width() * uvRect.width();
        vOffset = halfWidth / rect.height() * uvRect.height();
        outUV = uvRect.makeOutset(uOffset, vOffset);
        inUV = uvRect.makeInset(uOffset, vOffset);
        assistUV = uvRect;
      }
      // If we have a degenerate stroking rect(ie the stroke is larger than inner rect) then we
      // make a degenerate inside rect to avoid double hitting.  We will also jam all the points
      // together when we render these rects.
      if (inSide.isEmpty()) {  // process degenerate rect
        inSide.left = inSide.right = rect.centerX();
        inSide.top = inSide.bottom = rect.centerY();
        if (hasUVCoord) {
          inUV.left = inUV.right = uvRects[i]->centerX();
          inUV.top = inUV.bottom = uvRects[i]->centerY();
        }
      }
      // For bevel-stroke, use 2 Rect instances(outside and outsideAssist)
      // to draw the outside of the octagon. Because there are 8 vertices on the outer
      // edge, while vertex number of inner edge is 4, the same as miter-stroke.
      if (lineJoin() == LineJoin::Bevel) {
        outSide.inset(0, halfWidth);
        outSideAssist.outset(0, halfWidth);
        if (hasUVCoord) {
          outUV.inset(0, vOffset);
          assistUV.outset(0, vOffset);
        }
      }

      const auto outQuad = Quad::MakeFrom(outSide, &viewMatrix);
      if (hasUVCoord) {
        uvQuad = Quad::MakeFrom(outUV);
      }
      writeQuad(vertices, index, outQuad, uvQuad, compressedColor);
      if (lineJoin() == LineJoin::Bevel) {
        const auto assistQuad = Quad::MakeFrom(outSideAssist, &viewMatrix);
        if (hasUVCoord) {
          uvQuad = Quad::MakeFrom(assistUV);
        }
        writeQuad(vertices, index, assistQuad, uvQuad, compressedColor);
      }
      const auto inQuad = Quad::MakeFrom(inSide, &viewMatrix);
      if (hasUVCoord) {
        uvQuad = Quad::MakeFrom(inUV);
      }
      writeQuad(vertices, index, inQuad, uvQuad, compressedColor);
    }
  }
};

class AARoundStrokeRectsVertexProvider final : public RectsVertexProvider {
  PlacementArray<Stroke> strokes = {};

 public:
  AARoundStrokeRectsVertexProvider(PlacementArray<RectRecord>&& rects,
                                   PlacementArray<Rect>&& uvRects, PlacementArray<Stroke>&& strokes,
                                   AAType aaType, bool hasUVCoord, bool hasColor,
                                   std::shared_ptr<BlockAllocator> reference,
                                   std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            UVSubsetMode::None, std::move(reference), std::move(colorSpace)),
        strokes(std::move(strokes)) {
    _lineJoin = LineJoin::Round;
  }

  size_t vertexCount() const override {
    size_t perVertexCount = 24;
    size_t perVertexDataSize = 7;  // x, y, coverage, EllipseOffsets(2), EllipseRadii(2)
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
    const auto aaType = static_cast<AAType>(bitFields.aaType);
    const auto hasUVCoord = bitFields.hasUVCoord;
    const auto hasColor = bitFields.hasColor;
    std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
    if (hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
      steps =
          std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                                 _dstColorSpace.get(), AlphaType::Premultiplied);
    }
    for (size_t i = 0; i < rects.size(); ++i) {
      const auto& stroke = strokes[i];
      const auto& record = rects[i];
      auto viewMatrix = record->viewMatrix;
      auto scales = viewMatrix.getAxisScales();
      auto rect = record->rect;
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        compressedColor = ToUByte4PMColor(record->color, steps.get());
      }
      rect.scale(scales.x, scales.y);
      viewMatrix.preScale(1.0f / scales.x, 1.0f / scales.y);
      Point strokeSize = {stroke->width, stroke->width};
      if (stroke->width > 0.0f) {
        strokeSize = {scales.x * stroke->width, scales.y * stroke->width};
      } else {
        strokeSize.set(1.0f, 1.0f);
      }
      auto xRadius = strokeSize.x * 0.5f;
      auto yRadius = strokeSize.y * 0.5f;
      float reciprocalRadii[2] = {1.0f / xRadius, 1.0f / yRadius};
      const auto aaBloat = aaType == AAType::MSAA ? FLOAT_SQRT2 : 0.5f;
      auto xOuterRadius = xRadius + aaBloat;
      auto yOuterRadius = yRadius + aaBloat;
      auto xMaxOffset = xOuterRadius / xRadius;
      auto yMaxOffset = yOuterRadius / yRadius;
      auto bounds = rect.makeOutset(xRadius + aaBloat, yRadius + aaBloat);
      const float yCoords[4] = {bounds.top, bounds.top + yOuterRadius, bounds.bottom - yOuterRadius,
                                bounds.bottom};

      // we're using inversesqrt() in shader, so can't be exactly 0
      float yOuterOffsets[4] = {yMaxOffset, FLOAT_NEARLY_ZERO, FLOAT_NEARLY_ZERO, yMaxOffset};
      float xOuterOffsets[4] = {xMaxOffset, FLOAT_NEARLY_ZERO, FLOAT_NEARLY_ZERO, xMaxOffset};
      std::array<float, 4> uCoords = {}, vCoords = {};
      auto uStep = 0.f, vStep = 0.f;
      if (hasUVCoord) {
        auto& uvRect = *uvRects[i];
        uStep = stroke->width * 0.5f / record->rect.width() * uvRect.width();
        vStep = stroke->width * 0.5f / record->rect.height() * uvRect.height();
        uCoords = {uvRect.left - uStep, uvRect.left, uvRect.right, uvRect.right + uStep};
        vCoords = {uvRect.top - vStep, uvRect.top, uvRect.bottom, uvRect.bottom + vStep};
      }
      //round corner mesh
      for (size_t j = 0; j < 4; ++j) {
        Point points[4] = {{bounds.left, yCoords[j]},
                           {bounds.left + xRadius, yCoords[j]},
                           {bounds.right - xRadius, yCoords[j]},
                           {bounds.right, yCoords[j]}};
        viewMatrix.mapPoints(points, 4);
        for (size_t k = 0; k < 4; ++k) {
          vertices[index++] = points[k].x;
          vertices[index++] = points[k].y;
          vertices[index++] = 1.0f;
          vertices[index++] = xOuterOffsets[j];
          vertices[index++] = yOuterOffsets[k];
          vertices[index++] = reciprocalRadii[0];
          vertices[index++] = reciprocalRadii[1];
          if (hasUVCoord) {
            vertices[index++] = uCoords[j];
            vertices[index++] = vCoords[k];
          }
          if (hasColor) {
            vertices[index++] = compressedColor;
          }
        }
      }

      //Inner anti-aliased stroke ring
      bounds = rect.makeInset(xRadius, yRadius);
      const auto isDegenerate = std::min(bounds.width(), bounds.height()) <= 1.0f;
      constexpr float padding = 0.5f;
      auto insetBounds = bounds.makeInset(padding, padding);
      auto outsetBounds = bounds.makeOutset(padding, padding);
      auto insetUV = insetBounds;
      auto outsetUV = outsetBounds;
      if (hasUVCoord) {
        const auto& mat = record->viewMatrix;
        auto uvPadding =
            1.0f / std::sqrt(mat.getScaleX() * mat.getScaleX() + mat.getSkewY() * mat.getSkewY());
        const auto uvRect = uvRects[i]->makeInset(uStep, vStep);
        insetUV = uvRect.makeInset(uvPadding, uvPadding);
        outsetUV = uvRect.makeOutset(uvPadding, uvPadding);
      }
      if (isDegenerate) {
        insetBounds.left = insetBounds.right = bounds.centerX();
        insetBounds.top = insetBounds.bottom = bounds.centerY();
        insetUV.setEmpty();
      }
      auto insetQuad = Quad::MakeFrom(insetBounds, &viewMatrix);
      auto outsetQuad = Quad::MakeFrom(outsetBounds, &viewMatrix);
      auto uvInsetQuad = Quad::MakeFrom(insetUV);
      auto uvOutsetQuad = Quad::MakeFrom(outsetUV);
      for (size_t j = 0; j < 2; ++j) {
        auto& quad = j == 0 ? outsetQuad : insetQuad;
        auto& uvQuad = j == 0 ? uvOutsetQuad : uvInsetQuad;
        auto coverage = j == 0 ? 1.0f : 0.0f;
        for (size_t k = 0; k < 4; ++k) {
          vertices[index++] = quad.point(k).x;
          vertices[index++] = quad.point(k).y;
          vertices[index++] = coverage;
          vertices[index++] = 0.0f;
          vertices[index++] = 0.0f;
          vertices[index++] = 1.0f;
          vertices[index++] = 1.0f;
          if (hasUVCoord) {
            vertices[index++] = uvQuad.point(k).x;
            vertices[index++] = uvQuad.point(k).y;
          }
          if (hasColor) {
            vertices[index++] = compressedColor;
          }
        }
      }
    }
  }
};

class NonAARoundStrokeRectsVertexProvider final : public RectsVertexProvider {
  PlacementArray<Stroke> strokes;

 public:
  NonAARoundStrokeRectsVertexProvider(PlacementArray<RectRecord>&& rects,
                                      PlacementArray<Rect>&& uvRects,
                                      PlacementArray<Stroke>&& strokes, AAType aaType,
                                      bool hasUVCoord, bool hasColor,
                                      std::shared_ptr<BlockAllocator> reference,
                                      std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RectsVertexProvider(std::move(rects), std::move(uvRects), aaType, hasUVCoord, hasColor,
                            UVSubsetMode::None, std::move(reference), std::move(colorSpace)),
        strokes(std::move(strokes)) {
    _lineJoin = LineJoin::Round;
  }

  size_t vertexCount() const override {
    size_t perVertexCount = 20;
    size_t perVertexDataSize = 4;  // x, y, EllipseOffsets(2)
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
    const auto hasUVCoord = bitFields.hasUVCoord;
    const auto hasColor = bitFields.hasColor;
    std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
    if (hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
      steps =
          std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                                 _dstColorSpace.get(), AlphaType::Premultiplied);
    }
    for (size_t i = 0; i < rects.size(); ++i) {
      const auto& stroke = strokes[i];
      const auto& record = rects[i];
      auto viewMatrix = record->viewMatrix;
      auto scales = viewMatrix.getAxisScales();
      auto rect = record->rect;
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        compressedColor = ToUByte4PMColor(record->color, steps.get());
      }
      rect.scale(scales.x, scales.y);
      viewMatrix.preScale(1.0f / scales.x, 1.0f / scales.y);
      Point strokeSize = {stroke->width, stroke->width};
      if (stroke->width > 0.0f) {
        strokeSize = {scales.x * stroke->width, scales.y * stroke->width};
      } else {
        strokeSize.set(1.0f, 1.0f);
      }
      const auto xRadius = strokeSize.x * 0.5f;
      const auto yRadius = strokeSize.y * 0.5f;
      auto bounds = rect.makeOutset(xRadius, yRadius);
      const float yCoords[4] = {bounds.top, bounds.top + yRadius, bounds.bottom - yRadius,
                                bounds.bottom};
      std::array<float, 4> uCoords = {}, vCoords = {};
      auto uStep = 0.0f, vStep = 0.0f;
      if (hasUVCoord) {
        auto& uvRect = *uvRects[i];
        uStep = stroke->width * 0.5f / record->rect.width() * uvRect.width();
        vStep = stroke->width * 0.5f / record->rect.height() * uvRect.height();
        uCoords = {uvRect.left - uStep, uvRect.left, uvRect.right, uvRect.right + uStep};
        vCoords = {uvRect.top - vStep, uvRect.top, uvRect.bottom, uvRect.bottom + vStep};
      }

      //round corner mesh
      constexpr float radii[4] = {1.0f, 0.0f, 0.0f, 1.0f};
      for (size_t j = 0; j < 4; ++j) {
        Point points[4] = {{bounds.left, yCoords[j]},
                           {bounds.left + xRadius, yCoords[j]},
                           {bounds.right - xRadius, yCoords[j]},
                           {bounds.right, yCoords[j]}};
        viewMatrix.mapPoints(points, 4);
        for (size_t k = 0; k < 4; ++k) {
          vertices[index++] = points[k].x;
          vertices[index++] = points[k].y;
          vertices[index++] = radii[j];
          vertices[index++] = radii[k];
          if (hasUVCoord) {
            vertices[index++] = uCoords[k];
            vertices[index++] = vCoords[k];
          }
          if (hasColor) {
            vertices[index++] = compressedColor;
          }
        }
      }

      //Inner aa stroke ring
      auto inBounds = rect.makeInset(xRadius, yRadius);
      auto inUV = inBounds;
      if (hasUVCoord) {
        inUV = *uvRects[i];
        inUV.inset(uStep, vStep);
      }
      if (inBounds.isEmpty()) {  //degenerate
        inBounds.left = inBounds.right = rect.centerX();
        inBounds.top = inBounds.bottom = rect.centerY();
        inUV.left = inUV.right = uvRects[i]->centerX();
        inUV.top = inUV.bottom = uvRects[i]->centerY();
      }
      auto inQuad = Quad::MakeFrom(inBounds, &viewMatrix);
      auto inUVQuad = Quad::MakeFrom(inUV);
      for (size_t k = 0; k < 4; ++k) {
        vertices[index++] = inQuad.point(k).x;
        vertices[index++] = inQuad.point(k).y;
        vertices[index++] = 0.0f;
        vertices[index++] = 0.0f;
        if (hasUVCoord) {
          vertices[index++] = inUVQuad.point(k).x;
          vertices[index++] = inUVQuad.point(k).y;
        }
        if (hasColor) {
          vertices[index++] = compressedColor;
        }
      }
    }
  }
};

PlacementPtr<RectsVertexProvider> RectsVertexProvider::MakeFrom(BlockAllocator* allocator,
                                                                const Rect& rect, AAType aaType) {
  if (rect.isEmpty()) {
    return nullptr;
  }
  auto record = allocator->make<RectRecord>(rect, Matrix::I());
  auto rects = allocator->makeArray<RectRecord>(&record, 1);
  auto uvRects = allocator->makeArray<Rect>(0);
  if (aaType == AAType::Coverage) {
    return allocator->make<AARectsVertexProvider>(std::move(rects), std::move(uvRects), aaType,
                                                  false, false, UVSubsetMode::None,
                                                  allocator->addReference());
  }
  return allocator->make<NonAARectsVertexProvider>(std::move(rects), std::move(uvRects), aaType,
                                                   false, false, UVSubsetMode::None,
                                                   allocator->addReference());
}

PlacementPtr<RectsVertexProvider> RectsVertexProvider::MakeFrom(
    BlockAllocator* allocator, std::vector<PlacementPtr<RectRecord>>&& rects,
    std::vector<PlacementPtr<Rect>>&& uvRects, AAType aaType, bool needUVCoord,
    UVSubsetMode subsetMode, std::vector<PlacementPtr<Stroke>>&& strokes,
    std::shared_ptr<ColorSpace> colorSpace) {
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
  auto rectArray = allocator->makeArray(std::move(rects));
  auto uvRectArray = allocator->makeArray(std::move(uvRects));
  if (strokes.empty()) {
    if (aaType == AAType::Coverage) {
      return allocator->make<AARectsVertexProvider>(
          std::move(rectArray), std::move(uvRectArray), aaType, needUVCoord, hasColor, subsetMode,
          allocator->addReference(), std::move(colorSpace));
    }
    return allocator->make<NonAARectsVertexProvider>(
        std::move(rectArray), std::move(uvRectArray), aaType, needUVCoord, hasColor, subsetMode,
        allocator->addReference(), std::move(colorSpace));
  }

  const auto isRound = strokes.front()->join == LineJoin::Round;
  auto strokeArray = allocator->makeArray(std::move(strokes));
  if (aaType == AAType::Coverage) {
    if (isRound) {
      return allocator->make<AARoundStrokeRectsVertexProvider>(
          std::move(rectArray), std::move(uvRectArray), std::move(strokeArray), aaType, needUVCoord,
          hasColor, allocator->addReference(), std::move(colorSpace));
    }
    return allocator->make<AAAngularStrokeRectsVertexProvider>(
        std::move(rectArray), std::move(uvRectArray), std::move(strokeArray), aaType, needUVCoord,
        hasColor, allocator->addReference(), std::move(colorSpace));
  }

  if (isRound) {
    return allocator->make<NonAARoundStrokeRectsVertexProvider>(
        std::move(rectArray), std::move(uvRectArray), std::move(strokeArray), aaType, needUVCoord,
        hasColor, allocator->addReference(), std::move(colorSpace));
  }
  return allocator->make<NonAAAngularStrokeRectsVertexProvider>(
      std::move(rectArray), std::move(uvRectArray), std::move(strokeArray), aaType, needUVCoord,
      hasColor, allocator->addReference(), std::move(colorSpace));
}

RectsVertexProvider::RectsVertexProvider(PlacementArray<RectRecord>&& rects,
                                         PlacementArray<Rect>&& uvRects, AAType aaType,
                                         bool hasUVCoord, bool hasColor, UVSubsetMode subsetMode,
                                         std::shared_ptr<BlockAllocator> reference,
                                         std::shared_ptr<ColorSpace> colorSpace)
    : VertexProvider(std::move(reference)), rects(std::move(rects)), uvRects(std::move(uvRects)),
      _dstColorSpace(std::move(colorSpace)) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.hasUVCoord = hasUVCoord;
  bitFields.hasColor = hasColor;
  bitFields.subsetMode = static_cast<uint8_t>(subsetMode);
}
}  // namespace tgfx
