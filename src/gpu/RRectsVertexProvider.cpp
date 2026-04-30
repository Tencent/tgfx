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

#include "RRectsVertexProvider.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
// We have three possible cases for geometry for a round rect.
//
// In the case of a normal fill or a stroke, we draw the round rect as a 9-patch:
//    ____________
//   |_|________|_|
//   | |        | |
//   | |        | |
//   | |        | |
//   |_|________|_|
//   |_|________|_|
//
// For strokes, we don't draw the center quad.
//
// For circular round rects, in the case where the stroke width is greater than twice
// the corner radius (over stroke), we add additional geometry to mark out the rectangle
// in the center. The shared vertices are duplicated, so we can set a different outer radius
// for the fill calculation.
//    ____________
//   |_|________|_|
//   | |\ ____ /| |
//   | | |    | | |
//   | | |____| | |
//   |_|/______\|_|
//   |_|________|_|
//
// We don't draw the center quad from the fill rect in this case.
//
// For filled rrects that need to provide a distance vector we reuse the overstroke
// geometry but make the inner rect degenerate (either a point or a horizontal or
// vertical line).

// Returns 1/value, or a large sentinel when value is exactly zero, to avoid division by zero in
// shaders.
static float FloatInvert(float value) {
  return value == 0.0f ? 1e6f : 1 / value;
}

struct StrokeParams {
  float halfStrokeX = 0.0f;
  float halfStrokeY = 0.0f;
};

// Applies viewMatrix axis scales to the rrect's local radii and stroke, and reduces viewMatrix to
// the residual rotation/translation.
static StrokeParams ApplyScales(RRect* rRect, Matrix* viewMatrix, const Point& scales,
                                const Stroke* stroke) {
  rRect->scale(scales.x, scales.y);
  viewMatrix->preScale(1 / scales.x, 1 / scales.y);
  StrokeParams params;
  if (stroke) {
    auto strokeWidth = stroke->width > 0.0f ? stroke->width : 1.0f / std::max(scales.x, scales.y);
    params.halfStrokeX = 0.5f * scales.x * strokeWidth;
    params.halfStrokeY = 0.5f * scales.y * strokeWidth;
  }
  return params;
}

// Returns a ColorSpaceXformSteps that converts from sRGB to dstColorSpace, or nullptr when
// per-vertex colors are absent or conversion is unnecessary.
static std::unique_ptr<ColorSpaceXformSteps> MakeColorSpaceXformStepsIfNeeded(
    bool hasColor, const std::shared_ptr<ColorSpace>& dstColorSpace) {
  if (!hasColor || !NeedConvertColorSpace(ColorSpace::SRGB(), dstColorSpace)) {
    return nullptr;
  }
  return std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                                dstColorSpace.get(), AlphaType::Premultiplied);
}

// Computes per-corner outer X/Y radii and their reciprocals (including inner radii reciprocals
// for stroked rects) for a complex RRect. Returns true if the stroke is too wide for any corner
// (inner radius <= 0). Only meaningful when stroke is true.
static bool ComputeComplexCornerRadii(const std::array<Point, 4>& radii,
                                      const StrokeParams& strokeParams, bool stroke,
                                      std::array<float, 4>& outerXRadii,
                                      std::array<float, 4>& outerYRadii,
                                      std::array<std::array<float, 4>, 4>& recipRadii) {
  auto isOverstroked = false;
  if (stroke) {
    for (size_t c = 0; c < 4; ++c) {
      auto innerX = radii[c].x - strokeParams.halfStrokeX;
      auto innerY = radii[c].y - strokeParams.halfStrokeY;
      if (innerX <= 0.0f || innerY <= 0.0f) {
        isOverstroked = true;
      }
      outerXRadii[c] = radii[c].x + strokeParams.halfStrokeX;
      outerYRadii[c] = radii[c].y + strokeParams.halfStrokeY;
      recipRadii[c][0] = FloatInvert(outerXRadii[c]);
      recipRadii[c][1] = FloatInvert(outerYRadii[c]);
      recipRadii[c][2] = std::min(FloatInvert(std::max(innerX, 0.0f)), 1e6f);
      recipRadii[c][3] = std::min(FloatInvert(std::max(innerY, 0.0f)), 1e6f);
    }
  } else {
    for (size_t c = 0; c < 4; ++c) {
      outerXRadii[c] = radii[c].x;
      outerYRadii[c] = radii[c].y;
      recipRadii[c][0] = FloatInvert(outerXRadii[c]);
      recipRadii[c][1] = FloatInvert(outerYRadii[c]);
      // Keep every element of recipRadii defined on return.
      recipRadii[c][2] = 0.0f;
      recipRadii[c][3] = 0.0f;
    }
  }
  return isOverstroked;
}

// Computes per-corner x/y coordinates for the 4x4 vertex grid of a complex RRect. Unlike the
// simple variant, the rectangle is still split into 9 regions, but the edge and center regions
// are arbitrary quadrilaterals rather than axis-aligned rectangles.
static void ComputeComplexCornerGridPositions(const Rect& bounds,
                                              const std::array<float, 4>& xOuterRadii,
                                              const std::array<float, 4>& yOuterRadii,
                                              std::array<std::array<float, 2>, 4>& xPos,
                                              std::array<std::array<float, 2>, 4>& yPos) {
  // TL(0): left side
  xPos[0][0] = bounds.left;
  xPos[0][1] = bounds.left + xOuterRadii[0];
  yPos[0][0] = bounds.top;
  yPos[0][1] = bounds.top + yOuterRadii[0];
  // TR(1): right side
  xPos[1][0] = bounds.right;
  xPos[1][1] = bounds.right - xOuterRadii[1];
  yPos[1][0] = bounds.top;
  yPos[1][1] = bounds.top + yOuterRadii[1];
  // BR(2): right-bottom
  xPos[2][0] = bounds.right;
  xPos[2][1] = bounds.right - xOuterRadii[2];
  yPos[2][0] = bounds.bottom;
  yPos[2][1] = bounds.bottom - yOuterRadii[2];
  // BL(3): left-bottom
  xPos[3][0] = bounds.left;
  xPos[3][1] = bounds.left + xOuterRadii[3];
  yPos[3][0] = bounds.bottom;
  yPos[3][1] = bounds.bottom - yOuterRadii[3];
}

// Returns the corner index (0=TL, 1=TR, 2=BR, 3=BL) for a given position in the 4x4 vertex grid
// of a complex RRect. Top half (row 0/1) maps to TL/TR, bottom half (row 2/3) maps to BL/BR.
static inline size_t ComplexCornerIndex(size_t row, size_t column) {
  static constexpr size_t table[2][2] = {
      {0, 1},  // top: TL, TR
      {3, 2},  // bottom: BL, BR
  };
  return table[row / 2][column / 2];
}

// Returns the x coordinate of a vertex at (corner, column) in the 4x4 vertex grid of a complex
// RRect. column 0/3 = outer edge of the corner, column 1/2 = inner cut line of the corner.
static inline float ComplexCornerCoordX(size_t corner, size_t column,
                                        const std::array<std::array<float, 2>, 4>& xPos) {
  return (column == 0 || column == 3) ? xPos[corner][0] : xPos[corner][1];
}

// Returns the y coordinate of a vertex at (corner, row) in the 4x4 vertex grid of a complex
// RRect. row 0/3 = outer edge of the corner, row 1/2 = inner cut line of the corner.
static inline float ComplexCornerCoordY(size_t corner, size_t row,
                                        const std::array<std::array<float, 2>, 4>& yPos) {
  return (row == 0 || row == 3) ? yPos[corner][0] : yPos[corner][1];
}

// Returns the signed distance from x to the vertical edge belonging to this corner (un-bloated
// rect boundary). TL(0)/BL(3) are adjacent to the left edge, TR(1)/BR(2) to the right edge.
static inline float ComplexCornerEdgeDistanceX(size_t corner, float x, const Rect& rectBounds) {
  return (corner == 0 || corner == 3) ? (x - rectBounds.left) : (rectBounds.right - x);
}

// Returns the signed distance from y to the horizontal edge belonging to this corner (un-bloated
// rect boundary). TL(0)/TR(1) are adjacent to the top edge, BR(2)/BL(3) to the bottom edge.
static inline float ComplexCornerEdgeDistanceY(size_t corner, float y, const Rect& rectBounds) {
  return (corner == 0 || corner == 1) ? (y - rectBounds.top) : (rectBounds.bottom - y);
}

class AARRectsVertexProvider final : public RRectsVertexProvider {
 public:
  AARRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType, bool hasColor,
                         bool hasComplex, PlacementArray<Stroke>&& strokes,
                         std::shared_ptr<BlockAllocator> reference,
                         std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RRectsVertexProvider(std::move(rects), aaType, hasColor, hasComplex, std::move(strokes),
                             std::move(reference), std::move(colorSpace)) {
  }

  size_t vertexCount() const override {
    // AA simple: 16 vertices per RRect.
    //   position(2) + ellipseOffset(2) + ellipseRadii(4) = 8
    //   Optional: color(1).
    size_t floatsPerVertex = 8;
    if (bitFields.hasColor) {
      floatsPerVertex += 1;
    }
    return rects.size() * 16 * floatsPerVertex;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    auto currentAAType = aaType();
    size_t currentIndex = 0;
    auto steps = MakeColorSpaceXformStepsIfNeeded(bitFields.hasColor, _dstColorSpace);

    for (auto& record : rects) {
      DEBUG_ASSERT(record->rRect.type() != RRect::Type::Complex);
      auto viewMatrix = record->viewMatrix;
      auto rRect = record->rRect;
      auto scales = viewMatrix.getAxisScales();
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        uint32_t uintColor = ToUintPMColor(record->color, steps.get());
        compressedColor = *reinterpret_cast<float*>(&uintColor);
      }

      auto stroke = strokes.size() > currentIndex ? strokes[currentIndex].get() : nullptr;
      auto strokeParams = ApplyScales(&rRect, &viewMatrix, scales, stroke);

      bool stroked = false;
      float xRadius = rRect.radii()[0].x;
      float yRadius = rRect.radii()[0].y;
      float innerXRadius = 0;
      float innerYRadius = 0;
      auto rectBounds = rRect.rect();
      if (stroke) {
        innerXRadius = xRadius - strokeParams.halfStrokeX;
        innerYRadius = yRadius - strokeParams.halfStrokeY;
        stroked = innerXRadius > 0.0f && innerYRadius > 0.0f;
        xRadius += strokeParams.halfStrokeX;
        yRadius += strokeParams.halfStrokeY;
        rectBounds.outset(strokeParams.halfStrokeX, strokeParams.halfStrokeY);
      }

      float reciprocalRadii[4] = {FloatInvert(xRadius), FloatInvert(yRadius),
                                  FloatInvert(innerXRadius), FloatInvert(innerYRadius)};
      // If the stroke width is exactly double the radius, the inner radii will be zero.
      // Pin to a large value, to avoid infinities in the shader. crbug.com/1139750
      reciprocalRadii[2] = std::min(reciprocalRadii[2], 1e6f);
      reciprocalRadii[3] = std::min(reciprocalRadii[3], 1e6f);
      // On MSAA, bloat enough to guarantee any pixel that might be touched by the rRect has
      // full sample coverage.
      float aaBloat = currentAAType == AAType::MSAA ? FLOAT_SQRT2 : .5f;
      // Extend out the radii to antialias.
      float aaOuterXRadius = xRadius + aaBloat;
      float aaOuterYRadius = yRadius + aaBloat;

      float xMaxOffset = aaOuterXRadius;
      float yMaxOffset = aaOuterYRadius;
      if (!stroked) {
        // For filled RRectRecords we map a unit circle in the vertex attributes rather than
        // computing an ellipse and modifying that distance, so we normalize to 1.
        xMaxOffset /= xRadius;
        yMaxOffset /= yRadius;
      }
      auto aaBounds = rectBounds.makeOutset(aaBloat, aaBloat);
      float yCoords[4] = {aaBounds.top, aaBounds.top + aaOuterYRadius,
                          aaBounds.bottom - aaOuterYRadius, aaBounds.bottom};
      float yOuterOffsets[4] = {
          yMaxOffset,
          FLOAT_NEARLY_ZERO,  // we're using inversesqrt() in shader, so can't be exactly 0
          FLOAT_NEARLY_ZERO, yMaxOffset};
      for (int i = 0; i < 4; ++i) {
        auto point = Point::Make(aaBounds.left, yCoords[i]);
        viewMatrix.mapPoints(&point, 1);
        vertices[index++] = point.x;
        vertices[index++] = point.y;
        if (bitFields.hasColor) {
          vertices[index++] = compressedColor;
        }
        vertices[index++] = xMaxOffset;
        vertices[index++] = yOuterOffsets[i];
        vertices[index++] = reciprocalRadii[0];
        vertices[index++] = reciprocalRadii[1];
        vertices[index++] = reciprocalRadii[2];
        vertices[index++] = reciprocalRadii[3];

        point = Point::Make(aaBounds.left + aaOuterXRadius, yCoords[i]);
        viewMatrix.mapPoints(&point, 1);
        vertices[index++] = point.x;
        vertices[index++] = point.y;
        if (bitFields.hasColor) {
          vertices[index++] = compressedColor;
        }
        vertices[index++] = FLOAT_NEARLY_ZERO;
        vertices[index++] = yOuterOffsets[i];
        vertices[index++] = reciprocalRadii[0];
        vertices[index++] = reciprocalRadii[1];
        vertices[index++] = reciprocalRadii[2];
        vertices[index++] = reciprocalRadii[3];

        point = Point::Make(aaBounds.right - aaOuterXRadius, yCoords[i]);
        viewMatrix.mapPoints(&point, 1);
        vertices[index++] = point.x;
        vertices[index++] = point.y;
        if (bitFields.hasColor) {
          vertices[index++] = compressedColor;
        }
        vertices[index++] = FLOAT_NEARLY_ZERO;
        vertices[index++] = yOuterOffsets[i];
        vertices[index++] = reciprocalRadii[0];
        vertices[index++] = reciprocalRadii[1];
        vertices[index++] = reciprocalRadii[2];
        vertices[index++] = reciprocalRadii[3];

        point = Point::Make(aaBounds.right, yCoords[i]);
        viewMatrix.mapPoints(&point, 1);
        vertices[index++] = point.x;
        vertices[index++] = point.y;
        if (bitFields.hasColor) {
          vertices[index++] = compressedColor;
        }
        vertices[index++] = xMaxOffset;
        vertices[index++] = yOuterOffsets[i];
        vertices[index++] = reciprocalRadii[0];
        vertices[index++] = reciprocalRadii[1];
        vertices[index++] = reciprocalRadii[2];
        vertices[index++] = reciprocalRadii[3];
      }
      currentIndex++;
    }
  }
};

class NonAARRectsVertexProvider final : public RRectsVertexProvider {
 public:
  NonAARRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType, bool hasColor,
                            bool hasComplex, PlacementArray<Stroke>&& strokes,
                            std::shared_ptr<BlockAllocator> reference,
                            std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RRectsVertexProvider(std::move(rects), aaType, hasColor, hasComplex, std::move(strokes),
                             std::move(reference), std::move(colorSpace)) {
  }

  size_t vertexCount() const override {
    // Non-AA simple: 4 vertices per RRect.
    //   position(2) + localCoord(2) + radii(2) + rectBounds(4) = 10
    //   Optional: color(1), halfStrokeWidth(2)
    size_t floatsPerVertex = 10;
    if (bitFields.hasStroke) {
      floatsPerVertex += 2;
    }
    if (bitFields.hasColor) {
      floatsPerVertex += 1;
    }
    return rects.size() * 4 * floatsPerVertex;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    auto steps = MakeColorSpaceXformStepsIfNeeded(bitFields.hasColor, _dstColorSpace);

    size_t currentIndex = 0;
    for (auto& record : rects) {
      DEBUG_ASSERT(record->rRect.type() != RRect::Type::Complex);
      auto viewMatrix = record->viewMatrix;
      auto rRect = record->rRect;
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        uint32_t uintColor = ToUintPMColor(record->color, steps.get());
        compressedColor = *reinterpret_cast<float*>(&uintColor);
      }

      auto scales = viewMatrix.getAxisScales();
      auto stroke = strokes.size() > currentIndex ? strokes[currentIndex].get() : nullptr;
      auto strokeParams = ApplyScales(&rRect, &viewMatrix, scales, stroke);

      auto rect = rRect.rect();
      auto outerRadii = rRect.radii();
      if (stroke) {
        rect.outset(strokeParams.halfStrokeX, strokeParams.halfStrokeY);
        // Simple RRects have identical radii across all four corners, so only the first needs
        // update.
        outerRadii[0].x += strokeParams.halfStrokeX;
        outerRadii[0].y += strokeParams.halfStrokeY;
      }

      // Corner positions for a quad: TL, TR, BR, BL
      const Point corners[] = {
          {rect.left, rect.top},
          {rect.right, rect.top},
          {rect.right, rect.bottom},
          {rect.left, rect.bottom},
      };

      // Write 4 vertices for the quad
      for (int v = 0; v < 4; ++v) {
        auto localX = corners[v].x;
        auto localY = corners[v].y;
        auto point = Point::Make(localX, localY);
        viewMatrix.mapPoints(&point, 1);
        // position (2 floats)
        vertices[index++] = point.x;
        vertices[index++] = point.y;
        // localCoord (2 floats) - coordinates within the rect for shape evaluation
        vertices[index++] = localX;
        vertices[index++] = localY;
        // radii (2 floats) - uniform outer radii
        vertices[index++] = outerRadii[0].x;
        vertices[index++] = outerRadii[0].y;
        // rectBounds (4 floats)
        vertices[index++] = rect.left;
        vertices[index++] = rect.top;
        vertices[index++] = rect.right;
        vertices[index++] = rect.bottom;
        // Optional color
        if (bitFields.hasColor) {
          vertices[index++] = compressedColor;
        }
        // strokeWidth (2 floats) - only for stroke mode
        if (bitFields.hasStroke) {
          vertices[index++] = strokeParams.halfStrokeX;
          vertices[index++] = strokeParams.halfStrokeY;
        }
      }
      currentIndex++;
    }
  }
};

class AAComplexRRectsVertexProvider final : public RRectsVertexProvider {
 public:
  AAComplexRRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType, bool hasColor,
                                bool hasComplex, PlacementArray<Stroke>&& strokes,
                                std::shared_ptr<BlockAllocator> reference,
                                std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RRectsVertexProvider(std::move(rects), aaType, hasColor, hasComplex, std::move(strokes),
                             std::move(reference), std::move(colorSpace)) {
  }

  size_t vertexCount() const override {
    // AA complex: 16 vertices per RRect.
    //   position(2) + ellipseOffset(2) + ellipseRadii(4) + edgeDist(2) = 10
    //   Optional: color(1), halfStrokeWidth(2).
    size_t floatsPerVertex = 10;
    if (bitFields.hasStroke) {
      floatsPerVertex += 2;
    }
    if (bitFields.hasColor) {
      floatsPerVertex += 1;
    }
    return rects.size() * 16 * floatsPerVertex;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    auto currentAAType = aaType();
    size_t currentIndex = 0;
    auto steps = MakeColorSpaceXformStepsIfNeeded(bitFields.hasColor, _dstColorSpace);

    for (auto& record : rects) {
      DEBUG_ASSERT(record->rRect.type() == RRect::Type::Complex);
      auto viewMatrix = record->viewMatrix;
      auto rRect = record->rRect;
      auto scales = viewMatrix.getAxisScales();
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        uint32_t uintColor = ToUintPMColor(record->color, steps.get());
        compressedColor = *reinterpret_cast<float*>(&uintColor);
      }

      auto stroke = strokes.size() > currentIndex ? strokes[currentIndex].get() : nullptr;
      auto strokeParams = ApplyScales(&rRect, &viewMatrix, scales, stroke);

      std::array<float, 4> outerXRadii = {};
      std::array<float, 4> outerYRadii = {};
      std::array<std::array<float, 4>, 4> recipRadii;
      auto isOverstroked = ComputeComplexCornerRadii(rRect.radii(), strokeParams, stroke != nullptr,
                                                     outerXRadii, outerYRadii, recipRadii);
      // Overstroke RRects (inner radius <= 0) are not supported here and must be filtered
      // out by callers.
      DEBUG_ASSERT(!isOverstroked || !stroke);
      bool stroked = stroke != nullptr && !isOverstroked;
      auto rectBounds = rRect.rect();
      if (stroke) {
        rectBounds.outset(strokeParams.halfStrokeX, strokeParams.halfStrokeY);
      }

      float aaBloat = currentAAType == AAType::MSAA ? FLOAT_SQRT2 : .5f;
      auto aaBounds = rectBounds.makeOutset(aaBloat, aaBloat);
      std::array<float, 4> aaOuterXRadii = {};
      std::array<float, 4> aaOuterYRadii = {};
      for (size_t c = 0; c < 4; ++c) {
        aaOuterXRadii[c] = outerXRadii[c] + aaBloat;
        aaOuterYRadii[c] = outerYRadii[c] + aaBloat;
      }
      std::array<std::array<float, 2>, 4> xPos = {};
      std::array<std::array<float, 2>, 4> yPos = {};
      ComputeComplexCornerGridPositions(aaBounds, aaOuterXRadii, aaOuterYRadii, xPos, yPos);

      for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
          auto corner = ComplexCornerIndex(row, column);
          auto cornerXRadius = outerXRadii[corner];
          auto cornerYRadius = outerYRadii[corner];

          // Compute EllipseOffset — fill: normalized ellipse-space coordinate; stroke: raw offset
          // in pixels (normalized later in the fragment shader). 0.0 for directions not on an arc.
          auto xOffset = 0.0f;
          auto yOffset = 0.0f;
          if ((column == 0 || column == 3) && cornerXRadius > 0) {
            xOffset = stroked ? aaOuterXRadii[corner] : (aaOuterXRadii[corner] / cornerXRadius);
          }
          if ((row == 0 || row == 3) && cornerYRadius > 0) {
            yOffset = stroked ? aaOuterYRadii[corner] : (aaOuterYRadii[corner] / cornerYRadius);
          }

          // Vertex position uses per-corner coordinates
          auto localX = ComplexCornerCoordX(corner, column, xPos);
          auto localY = ComplexCornerCoordY(corner, row, yPos);
          auto point = Point::Make(localX, localY);
          viewMatrix.mapPoints(&point, 1);
          vertices[index++] = point.x;
          vertices[index++] = point.y;
          if (bitFields.hasColor) {
            vertices[index++] = compressedColor;
          }
          vertices[index++] = xOffset;
          vertices[index++] = yOffset;
          vertices[index++] = recipRadii[corner][0];
          vertices[index++] = recipRadii[corner][1];
          vertices[index++] = recipRadii[corner][2];
          vertices[index++] = recipRadii[corner][3];
          vertices[index++] = ComplexCornerEdgeDistanceX(corner, localX, rectBounds);
          vertices[index++] = ComplexCornerEdgeDistanceY(corner, localY, rectBounds);
          if (bitFields.hasStroke) {
            vertices[index++] = strokeParams.halfStrokeX;
            vertices[index++] = strokeParams.halfStrokeY;
          }
        }
      }
      currentIndex++;
    }
  }
};

class NonAAComplexRRectsVertexProvider final : public RRectsVertexProvider {
 public:
  NonAAComplexRRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType,
                                   bool hasColor, bool hasComplex, PlacementArray<Stroke>&& strokes,
                                   std::shared_ptr<BlockAllocator> reference,
                                   std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : RRectsVertexProvider(std::move(rects), aaType, hasColor, hasComplex, std::move(strokes),
                             std::move(reference), std::move(colorSpace)) {
  }

  size_t vertexCount() const override {
    // Non-AA complex: 4 vertices per RRect.
    //   position(2) + localCoord(2) + xRadii(4) + yRadii(4) + rectBounds(4) = 16
    //   Optional: color(1), halfStrokeWidth(2)
    size_t floatsPerVertex = 16;
    if (bitFields.hasStroke) {
      floatsPerVertex += 2;
    }
    if (bitFields.hasColor) {
      floatsPerVertex += 1;
    }
    return rects.size() * 4 * floatsPerVertex;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    auto steps = MakeColorSpaceXformStepsIfNeeded(bitFields.hasColor, _dstColorSpace);

    size_t currentIndex = 0;
    for (auto& record : rects) {
      auto viewMatrix = record->viewMatrix;
      auto rRect = record->rRect;
      float compressedColor = 0.f;
      if (bitFields.hasColor) {
        uint32_t uintColor = ToUintPMColor(record->color, steps.get());
        compressedColor = *reinterpret_cast<float*>(&uintColor);
      }

      auto scales = viewMatrix.getAxisScales();
      auto stroke = strokes.size() > currentIndex ? strokes[currentIndex].get() : nullptr;
      auto strokeParams = ApplyScales(&rRect, &viewMatrix, scales, stroke);

      auto rect = rRect.rect();
      auto outerRadii = rRect.radii();
      if (stroke) {
        rect.outset(strokeParams.halfStrokeX, strokeParams.halfStrokeY);
        for (size_t i = 0; i < 4; ++i) {
          outerRadii[i].x += strokeParams.halfStrokeX;
          outerRadii[i].y += strokeParams.halfStrokeY;
        }
      }

      // Corner positions for a quad: TL, TR, BR, BL
      const Point corners[] = {
          {rect.left, rect.top},
          {rect.right, rect.top},
          {rect.right, rect.bottom},
          {rect.left, rect.bottom},
      };

      // Write 4 vertices for the quad
      for (int v = 0; v < 4; ++v) {
        auto localX = corners[v].x;
        auto localY = corners[v].y;
        auto point = Point::Make(localX, localY);
        viewMatrix.mapPoints(&point, 1);
        // position (2 floats)
        vertices[index++] = point.x;
        vertices[index++] = point.y;
        // localCoord (2 floats) - coordinates within the rect for shape evaluation
        vertices[index++] = localX;
        vertices[index++] = localY;
        // xRadii (4 floats) - per-corner x radii [TL, TR, BR, BL]
        vertices[index++] = outerRadii[0].x;
        vertices[index++] = outerRadii[1].x;
        vertices[index++] = outerRadii[2].x;
        vertices[index++] = outerRadii[3].x;
        // yRadii (4 floats) - per-corner y radii [TL, TR, BR, BL]
        vertices[index++] = outerRadii[0].y;
        vertices[index++] = outerRadii[1].y;
        vertices[index++] = outerRadii[2].y;
        vertices[index++] = outerRadii[3].y;
        // rectBounds (4 floats)
        vertices[index++] = rect.left;
        vertices[index++] = rect.top;
        vertices[index++] = rect.right;
        vertices[index++] = rect.bottom;
        // Optional color
        if (bitFields.hasColor) {
          vertices[index++] = compressedColor;
        }
        // strokeWidth (2 floats) - only for stroke mode
        if (bitFields.hasStroke) {
          vertices[index++] = strokeParams.halfStrokeX;
          vertices[index++] = strokeParams.halfStrokeY;
        }
      }
      currentIndex++;
    }
  }
};

PlacementPtr<RRectsVertexProvider> RRectsVertexProvider::MakeFrom(
    BlockAllocator* allocator, std::vector<PlacementPtr<RRectRecord>>&& rects, AAType aaType,
    std::vector<PlacementPtr<Stroke>>&& strokes, std::shared_ptr<ColorSpace> colorSpace) {
  if (rects.empty()) {
    return nullptr;
  }
  auto hasColor = false;
  auto hasComplex = false;
  auto& firstColor = rects.front()->color;
  for (auto& record : rects) {
    if (record->color != firstColor) {
      hasColor = true;
    }
    if (record->rRect.type() == RRect::Type::Complex) {
      hasComplex = true;
    }
    if (hasColor && hasComplex) {
      break;
    }
  }
  auto array = allocator->makeArray(std::move(rects));
  auto strokeArray = allocator->makeArray(std::move(strokes));
  if (aaType == AAType::None) {
    if (hasComplex) {
      return allocator->make<NonAAComplexRRectsVertexProvider>(
          std::move(array), aaType, hasColor, hasComplex, std::move(strokeArray),
          allocator->addReference(), std::move(colorSpace));
    }
    return allocator->make<NonAARRectsVertexProvider>(
        std::move(array), aaType, hasColor, hasComplex, std::move(strokeArray),
        allocator->addReference(), std::move(colorSpace));
  }
  if (hasComplex) {
    return allocator->make<AAComplexRRectsVertexProvider>(
        std::move(array), aaType, hasColor, hasComplex, std::move(strokeArray),
        allocator->addReference(), std::move(colorSpace));
  }
  return allocator->make<AARRectsVertexProvider>(std::move(array), aaType, hasColor, hasComplex,
                                                 std::move(strokeArray), allocator->addReference(),
                                                 std::move(colorSpace));
}

RRectsVertexProvider::RRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType,
                                           bool hasColor, bool hasComplex,
                                           PlacementArray<Stroke>&& strokes,
                                           std::shared_ptr<BlockAllocator> reference,
                                           std::shared_ptr<ColorSpace> colorSpace)
    : VertexProvider(std::move(reference)), rects(std::move(rects)), strokes(std::move(strokes)),
      _dstColorSpace(std::move(colorSpace)) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.hasColor = hasColor;
  bitFields.hasStroke = !this->strokes.empty();
  bitFields.isComplex = hasComplex;
}
}  // namespace tgfx
