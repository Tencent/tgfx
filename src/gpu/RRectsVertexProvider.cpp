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

#include "RRectsVertexProvider.h"
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
    BlockBuffer* buffer, std::vector<PlacementPtr<RRectRecord>>&& rects, AAType aaType,
    bool useScale) {
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
  auto array = buffer->makeArray(std::move(rects));
  return buffer->make<RRectsVertexProvider>(std::move(array), aaType, useScale, hasColor);
}

static void WriteUByte4Color(float* vertices, int& index, const Color& color) {
  auto bytes = reinterpret_cast<uint8_t*>(&vertices[index++]);
  bytes[0] = static_cast<uint8_t>(color.red * 255);
  bytes[1] = static_cast<uint8_t>(color.green * 255);
  bytes[2] = static_cast<uint8_t>(color.blue * 255);
  bytes[3] = static_cast<uint8_t>(color.alpha * 255);
}

RRectsVertexProvider::RRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType,
                                           bool useScale, bool hasColor)
    : rects(std::move(rects)) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.useScale = useScale;
  bitFields.hasColor = hasColor;
}

size_t RRectsVertexProvider::vertexCount() const {
  auto floatCount = rects.size() * 4 * 36;
  if (bitFields.useScale) {
    floatCount += rects.size() * 4 * 4;
  }
  return floatCount;
}

void RRectsVertexProvider::getVertices(float* vertices) const {
  auto index = 0;
  auto aaType = static_cast<AAType>(bitFields.aaType);
  for (auto& record : rects) {
    auto viewMatrix = record->viewMatrix;
    auto rRect = record->rRect;
    auto& color = record->color;
    auto scales = viewMatrix.getAxisScales();
    rRect.scale(scales.x, scales.y);
    viewMatrix.preScale(1 / scales.x, 1 / scales.y);
    float reciprocalRadii[4] = {1e6f, 1e6f, 1e6f, 1e6f};
    if (rRect.radii.x > 0) {
      reciprocalRadii[0] = 1.f / rRect.radii.x;
    }
    if (rRect.radii.y > 0) {
      reciprocalRadii[1] = 1.f / rRect.radii.y;
    }
    // On MSAA, bloat enough to guarantee any pixel that might be touched by the rRect has
    // full sample coverage.
    float aaBloat = aaType == AAType::MSAA ? FLOAT_SQRT2 : .5f;
    // Extend out the radii to antialias.
    float xOuterRadius = rRect.radii.x + aaBloat;
    float yOuterRadius = rRect.radii.y + aaBloat;

    float xMaxOffset = xOuterRadius;
    float yMaxOffset = yOuterRadius;
    //  if (!stroked) {
    // For filled RRectRecords we map a unit circle in the vertex attributes rather than
    // computing an ellipse and modifying that distance, so we normalize to 1.
    xMaxOffset /= rRect.radii.x;
    yMaxOffset /= rRect.radii.y;
    //  }
    auto bounds = rRect.rect.makeOutset(aaBloat, aaBloat);
    float yCoords[4] = {bounds.top, bounds.top + yOuterRadius, bounds.bottom - yOuterRadius,
                        bounds.bottom};
    float yOuterOffsets[4] = {
        yMaxOffset,
        FLOAT_NEARLY_ZERO,  // we're using inversesqrt() in shader, so can't be exactly 0
        FLOAT_NEARLY_ZERO, yMaxOffset};
    auto maxRadius = std::max(rRect.radii.x, rRect.radii.y);
    for (int i = 0; i < 4; ++i) {
      auto point = Point::Make(bounds.left, yCoords[i]);
      viewMatrix.mapPoints(&point, 1);
      vertices[index++] = point.x;
      vertices[index++] = point.y;
      if (bitFields.hasColor) {
        WriteUByte4Color(vertices, index, color);
      }
      vertices[index++] = xMaxOffset;
      vertices[index++] = yOuterOffsets[i];
      if (bitFields.useScale) {
        vertices[index++] = maxRadius;
      }
      vertices[index++] = reciprocalRadii[0];
      vertices[index++] = reciprocalRadii[1];
      vertices[index++] = reciprocalRadii[2];
      vertices[index++] = reciprocalRadii[3];

      point = Point::Make(bounds.left + xOuterRadius, yCoords[i]);
      viewMatrix.mapPoints(&point, 1);
      vertices[index++] = point.x;
      vertices[index++] = point.y;
      if (bitFields.hasColor) {
        WriteUByte4Color(vertices, index, color);
      }
      vertices[index++] = FLOAT_NEARLY_ZERO;
      vertices[index++] = yOuterOffsets[i];
      if (bitFields.useScale) {
        vertices[index++] = maxRadius;
      }
      vertices[index++] = reciprocalRadii[0];
      vertices[index++] = reciprocalRadii[1];
      vertices[index++] = reciprocalRadii[2];
      vertices[index++] = reciprocalRadii[3];

      point = Point::Make(bounds.right - xOuterRadius, yCoords[i]);
      viewMatrix.mapPoints(&point, 1);
      vertices[index++] = point.x;
      vertices[index++] = point.y;
      if (bitFields.hasColor) {
        WriteUByte4Color(vertices, index, color);
      }
      vertices[index++] = FLOAT_NEARLY_ZERO;
      vertices[index++] = yOuterOffsets[i];
      if (bitFields.useScale) {
        vertices[index++] = maxRadius;
      }
      vertices[index++] = reciprocalRadii[0];
      vertices[index++] = reciprocalRadii[1];
      vertices[index++] = reciprocalRadii[2];
      vertices[index++] = reciprocalRadii[3];

      point = Point::Make(bounds.right, yCoords[i]);
      viewMatrix.mapPoints(&point, 1);
      vertices[index++] = point.x;
      vertices[index++] = point.y;
      if (bitFields.hasColor) {
        WriteUByte4Color(vertices, index, color);
      }
      vertices[index++] = xMaxOffset;
      vertices[index++] = yOuterOffsets[i];
      if (bitFields.useScale) {
        vertices[index++] = maxRadius;
      }
      vertices[index++] = reciprocalRadii[0];
      vertices[index++] = reciprocalRadii[1];
      vertices[index++] = reciprocalRadii[2];
      vertices[index++] = reciprocalRadii[3];
    }
  }
}
}  // namespace tgfx
