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

PlacementPtr<RRectsVertexProvider> RRectsVertexProvider::MakeFrom(
    BlockAllocator* allocator, std::vector<PlacementPtr<RRectRecord>>&& rects, AAType aaType,
    std::vector<PlacementPtr<Stroke>>&& strokes, std::shared_ptr<ColorSpace> colorSpace) {
  if (rects.empty()) {
    return nullptr;
  }
  auto hasColor = false;
  if (rects.size() > 1) {
    auto& firstColor = rects.front()->color;
    for (auto& record : rects) {
      if (record->color != firstColor) {
        hasColor = true;
        break;
      }
    }
  }
  auto array = allocator->makeArray(std::move(rects));
  auto strokeArray = allocator->makeArray(std::move(strokes));
  return allocator->make<RRectsVertexProvider>(std::move(array), aaType, hasColor,
                                               std::move(strokeArray), allocator->addReference(),
                                               std::move(colorSpace));
}

static float FloatInvert(float value) {
  return value == 0.0f ? 1e6f : 1 / value;
}

struct StrokeParams {
  float halfStrokeX = 0.0f;
  float halfStrokeY = 0.0f;
};

static StrokeParams ApplyScales(RRect* rRect, Matrix* viewMatrix, const Point& scales,
                                const Stroke* stroke) {
  rRect->scale(scales.x, scales.y);
  viewMatrix->preScale(1 / scales.x, 1 / scales.y);
  StrokeParams params;
  if (stroke) {
    auto strokeWidth = stroke->width > 0.0f ? stroke->width : 1.0f / std::max(scales.x, scales.y);
    params.halfStrokeX = 0.5f * scales.x * strokeWidth;
    params.halfStrokeY = 0.5f * scales.y * strokeWidth;
    if (viewMatrix->getScaleX() == 0.f) {
      for (auto& r : rRect->radii) {
        std::swap(r.x, r.y);
      }
      std::swap(params.halfStrokeX, params.halfStrokeY);
    }
  }
  return params;
}

RRectsVertexProvider::RRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType,
                                           bool hasColor, PlacementArray<Stroke>&& strokes,
                                           std::shared_ptr<BlockAllocator> reference,
                                           std::shared_ptr<ColorSpace> colorSpace)
    : VertexProvider(std::move(reference)), rects(std::move(rects)), strokes(std::move(strokes)),
      _dstColorSpace(std::move(colorSpace)) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.hasColor = hasColor;
  bitFields.hasStroke = !this->strokes.empty();
}

size_t RRectsVertexProvider::vertexCount() const {
  if (aaType() == AAType::None) {
    // Non-AA mode: 4 vertices per RRect
    // Each vertex has: position (2), localCoord (2), xRadii (4), yRadii (4), rectBounds (4)
    // Optional: color (1), strokeWidth (2) for stroke mode
    size_t floatsPerVertex = 16;
    if (bitFields.hasStroke) {
      floatsPerVertex += 2;
    }
    if (bitFields.hasColor) {
      floatsPerVertex += 1;
    }
    return rects.size() * 4 * floatsPerVertex;
  }
  // AA mode: 16 vertices per RRect (4x4 grid)
  auto floatCount = rects.size() * 4 * 32;
  if (bitFields.hasColor) {
    floatCount += rects.size() * 4 * 4;
  }
  return floatCount;
}

void RRectsVertexProvider::getVertices(float* vertices) const {
  if (aaType() == AAType::None) {
    getNonAAVertices(vertices);
  } else {
    getAAVertices(vertices);
  }
}

void RRectsVertexProvider::getAAVertices(float* vertices) const {
  size_t index = 0;
  auto currentAAType = aaType();
  size_t currentIndex = 0;
  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (bitFields.hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                               _dstColorSpace.get(), AlphaType::Premultiplied);
  }
  for (auto& record : rects) {
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

    // Per-corner outer and inner radii, and reciprocal radii arrays.
    // Corner order: TL=0, TR=1, BR=2, BL=3
    float outerXRadii[4] = {};
    float outerYRadii[4] = {};
    float recipRadii[4][4] = {};  // [corner][outerRX, outerRY, innerRX, innerRY]
    bool stroked = false;
    auto rectBounds = rRect.rect;

    if (stroke) {
      auto allInnerPositive = true;
      for (size_t c = 0; c < 4; ++c) {
        auto innerX = rRect.radii[c].x - strokeParams.halfStrokeX;
        auto innerY = rRect.radii[c].y - strokeParams.halfStrokeY;
        if (innerX <= 0.0f || innerY <= 0.0f) {
          allInnerPositive = false;
        }
        outerXRadii[c] = rRect.radii[c].x + strokeParams.halfStrokeX;
        outerYRadii[c] = rRect.radii[c].y + strokeParams.halfStrokeY;
        recipRadii[c][0] = FloatInvert(outerXRadii[c]);
        recipRadii[c][1] = FloatInvert(outerYRadii[c]);
        recipRadii[c][2] = std::min(FloatInvert(std::max(innerX, 0.0f)), 1e6f);
        recipRadii[c][3] = std::min(FloatInvert(std::max(innerY, 0.0f)), 1e6f);
      }
      stroked = allInnerPositive;
      rectBounds.outset(strokeParams.halfStrokeX, strokeParams.halfStrokeY);
    } else {
      for (size_t c = 0; c < 4; ++c) {
        outerXRadii[c] = rRect.radii[c].x;
        outerYRadii[c] = rRect.radii[c].y;
        recipRadii[c][0] = FloatInvert(outerXRadii[c]);
        recipRadii[c][1] = FloatInvert(outerYRadii[c]);
        recipRadii[c][2] = std::min(FloatInvert(0.0f), 1e6f);
        recipRadii[c][3] = std::min(FloatInvert(0.0f), 1e6f);
      }
    }

    float aaBloat = currentAAType == AAType::MSAA ? FLOAT_SQRT2 : .5f;

    // 9-patch cut points: use max of adjacent corners for each side.
    auto xOuterRadiusLeft = std::max(outerXRadii[0], outerXRadii[3]);    // max(TL, BL)
    auto xOuterRadiusRight = std::max(outerXRadii[1], outerXRadii[2]);   // max(TR, BR)
    auto yOuterRadiusTop = std::max(outerYRadii[0], outerYRadii[1]);     // max(TL, TR)
    auto yOuterRadiusBottom = std::max(outerYRadii[2], outerYRadii[3]);  // max(BR, BL)

    auto bounds = rectBounds.makeOutset(aaBloat, aaBloat);

    // 4 rows: top edge, top inner, bottom inner, bottom edge
    float yCoords[4] = {bounds.top, bounds.top + yOuterRadiusTop + aaBloat,
                        bounds.bottom - yOuterRadiusBottom - aaBloat, bounds.bottom};

    // 4 columns: left edge, left inner, right inner, right edge
    float xCoords[4] = {bounds.left, bounds.left + xOuterRadiusLeft + aaBloat,
                        bounds.right - xOuterRadiusRight - aaBloat, bounds.right};

    // Corner assignment for each of the 16 vertices in 4x4 grid.
    // [row][col] → corner index (TL=0, TR=1, BR=2, BL=3)
    // Row 0-1 are top rows, Row 2-3 are bottom rows.
    // Col 0-1 are left cols, Col 2-3 are right cols.
    static constexpr int cornerMap[4][4] = {
        {0, 0, 1, 1},  // row 0: TL, TL, TR, TR
        {0, 0, 1, 1},  // row 1: TL, TL, TR, TR
        {3, 3, 2, 2},  // row 2: BL, BL, BR, BR
        {3, 3, 2, 2},  // row 3: BL, BL, BR, BR
    };

    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        auto corner = cornerMap[row][col];
        auto cornerXRadius = outerXRadii[corner];
        auto cornerYRadius = outerYRadii[corner];

        // Compute EllipseOffset for this vertex.
        // For corners with non-zero radii, outer vertices get the max offset normalized
        // to unit circle coordinates. For corners with zero radii (sharp corners), offset
        // stays near-zero so coverage saturates to 1.0.
        auto xOffset = FLOAT_NEARLY_ZERO;
        auto yOffset = FLOAT_NEARLY_ZERO;
        if ((col == 0 || col == 3) && cornerXRadius > 0) {
          xOffset = stroked ? (cornerXRadius + aaBloat)
                            : ((cornerXRadius + aaBloat) / cornerXRadius);
        }
        if ((row == 0 || row == 3) && cornerYRadius > 0) {
          yOffset = stroked ? (cornerYRadius + aaBloat)
                            : ((cornerYRadius + aaBloat) / cornerYRadius);
        }

        auto point = Point::Make(xCoords[col], yCoords[row]);
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
      }
    }
    currentIndex++;
  }
}

void RRectsVertexProvider::getNonAAVertices(float* vertices) const {
  size_t index = 0;
  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (bitFields.hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                               _dstColorSpace.get(), AlphaType::Premultiplied);
  }

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

    auto rect = rRect.rect;
    auto radii = rRect.radii;

    if (stroke) {
      rect.outset(strokeParams.halfStrokeX, strokeParams.halfStrokeY);
      for (auto& r : radii) {
        r.x += strokeParams.halfStrokeX;
        r.y += strokeParams.halfStrokeY;
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
      float localX = corners[v].x;
      float localY = corners[v].y;

      auto point = Point::Make(localX, localY);
      viewMatrix.mapPoints(&point, 1);

      // position (2 floats)
      vertices[index++] = point.x;
      vertices[index++] = point.y;

      // localCoord (2 floats) - coordinates within the rect for shape evaluation
      vertices[index++] = localX;
      vertices[index++] = localY;

      // xRadii (4 floats) - per-corner x radii [TL, TR, BR, BL]
      vertices[index++] = radii[0].x;
      vertices[index++] = radii[1].x;
      vertices[index++] = radii[2].x;
      vertices[index++] = radii[3].x;

      // yRadii (4 floats) - per-corner y radii [TL, TR, BR, BL]
      vertices[index++] = radii[0].y;
      vertices[index++] = radii[1].y;
      vertices[index++] = radii[2].y;
      vertices[index++] = radii[3].y;

      // rectBounds (4 floats) - outer bounds
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
}  // namespace tgfx
