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
    // Each vertex has: position (2), localCoord (2), radii (2), rectBounds (4)
    // Optional: color (1), strokeWidth (2) for stroke mode
    size_t floatsPerVertex = 10;
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
    rRect.scale(scales.x, scales.y);
    viewMatrix.preScale(1 / scales.x, 1 / scales.y);

    bool stroked = false;
    auto stroke = strokes.size() > currentIndex ? strokes[currentIndex].get() : nullptr;
    float xRadius = rRect.radii.x;
    float yRadius = rRect.radii.y;
    float innerXRadius = 0;
    float innerYRadius = 0;
    auto rectBounds = rRect.rect;
    if (stroke) {
      // For hairline stroke (width == 0), use 1 pixel width in device space.
      auto strokeWidth = stroke->width > 0.0f ? stroke->width : 1.0f / std::max(scales.x, scales.y);
      Point halfStrokeWidth = {0.5f * scales.x * strokeWidth, 0.5f * scales.y * strokeWidth};
      if (viewMatrix.getScaleX() == 0.f) {
        // The matrix may have a rotation by an odd multiple of 90 degrees.
        std::swap(xRadius, yRadius);
        std::swap(halfStrokeWidth.x, halfStrokeWidth.y);
      }
      innerXRadius = xRadius - halfStrokeWidth.x;
      innerYRadius = yRadius - halfStrokeWidth.y;
      stroked = innerXRadius > 0.0f && innerYRadius > 0.0f;
      xRadius += halfStrokeWidth.x;
      yRadius += halfStrokeWidth.y;
      rectBounds.outset(halfStrokeWidth.x, halfStrokeWidth.y);
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
    float xOuterRadius = xRadius + aaBloat;
    float yOuterRadius = yRadius + aaBloat;

    float xMaxOffset = xOuterRadius;
    float yMaxOffset = yOuterRadius;
    if (!stroked) {
      // For filled RRectRecords we map a unit circle in the vertex attributes rather than
      // computing an ellipse and modifying that distance, so we normalize to 1.
      xMaxOffset /= xRadius;
      yMaxOffset /= yRadius;
    }
    auto bounds = rectBounds.makeOutset(aaBloat, aaBloat);
    float yCoords[4] = {bounds.top, bounds.top + yOuterRadius, bounds.bottom - yOuterRadius,
                        bounds.bottom};
    float yOuterOffsets[4] = {
        yMaxOffset,
        FLOAT_NEARLY_ZERO,  // we're using inversesqrt() in shader, so can't be exactly 0
        FLOAT_NEARLY_ZERO, yMaxOffset};
    for (int i = 0; i < 4; ++i) {
      auto point = Point::Make(bounds.left, yCoords[i]);
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

      point = Point::Make(bounds.left + xOuterRadius, yCoords[i]);
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

      point = Point::Make(bounds.right - xOuterRadius, yCoords[i]);
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

      point = Point::Make(bounds.right, yCoords[i]);
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

void RRectsVertexProvider::getNonAAVertices(float* vertices) const {
  size_t index = 0;
  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (bitFields.hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                               _dstColorSpace.get(), AlphaType::Premultiplied);
  }

  // Corner positions for a quad: TL, TR, BR, BL
  static constexpr float cornerX[] = {0.0f, 1.0f, 1.0f, 0.0f};
  static constexpr float cornerY[] = {0.0f, 0.0f, 1.0f, 1.0f};

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
    rRect.scale(scales.x, scales.y);
    viewMatrix.preScale(1 / scales.x, 1 / scales.y);

    auto rect = rRect.rect;
    auto stroke = strokes.size() > currentIndex ? strokes[currentIndex].get() : nullptr;

    float xRadii = rRect.radii.x;
    float yRadii = rRect.radii.y;
    float halfStrokeX = 0.0f;
    float halfStrokeY = 0.0f;

    if (stroke) {
      // For hairline stroke (width == 0), use 1 pixel width in device space.
      auto strokeWidth = stroke->width > 0.0f ? stroke->width : 1.0f / std::max(scales.x, scales.y);
      halfStrokeX = 0.5f * scales.x * strokeWidth;
      halfStrokeY = 0.5f * scales.y * strokeWidth;
      if (viewMatrix.getScaleX() == 0.f) {
        std::swap(xRadii, yRadii);
        std::swap(halfStrokeX, halfStrokeY);
      }
      rect.outset(halfStrokeX, halfStrokeY);
      xRadii += halfStrokeX;
      yRadii += halfStrokeY;
    }

    auto left = rect.left;
    auto top = rect.top;
    auto right = rect.right;
    auto bottom = rect.bottom;

    // Write 4 vertices for the quad
    for (int v = 0; v < 4; ++v) {
      // Local position within rect [0, 1]
      float lx = cornerX[v];
      float ly = cornerY[v];

      // Position in local space
      float localX = left + lx * (right - left);
      float localY = top + ly * (bottom - top);

      // Transform to device space
      auto point = Point::Make(localX, localY);
      viewMatrix.mapPoints(&point, 1);

      // position (2 floats)
      vertices[index++] = point.x;
      vertices[index++] = point.y;

      // localCoord (2 floats) - coordinates within the rect for shape evaluation
      vertices[index++] = localX;
      vertices[index++] = localY;

      // radii (2 floats) - outer radii
      vertices[index++] = xRadii;
      vertices[index++] = yRadii;

      // rectBounds (4 floats) - outer bounds
      vertices[index++] = left;
      vertices[index++] = top;
      vertices[index++] = right;
      vertices[index++] = bottom;

      // Optional color
      if (bitFields.hasColor) {
        vertices[index++] = compressedColor;
      }

      // strokeWidth (2 floats) - only for stroke mode
      if (bitFields.hasStroke) {
        vertices[index++] = halfStrokeX;
        vertices[index++] = halfStrokeY;
      }
    }
    currentIndex++;
  }
}
}  // namespace tgfx
