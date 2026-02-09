/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "NonAARRectsVertexProvider.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/ColorSpaceHelper.h"

namespace tgfx {
// Vertex layout for NonAARRectOp:
// Each vertex contains:
// - position (float2): vertex position in device space
// - localCoord (float2): local coordinates for RRect shape evaluation
// - radii (float2): corner radii (outer radii for stroke)
// - rectBounds (float4): left, top, right, bottom of the rect (outer bounds for stroke)
// - strokeWidth (float2, stroke only): half stroke width in x and y
// - color (float, optional): Compressed premultiplied color

PlacementPtr<NonAARRectsVertexProvider> NonAARRectsVertexProvider::MakeFrom(
    BlockAllocator* allocator, std::vector<PlacementPtr<RRectRecord>>&& rects,
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
  return allocator->make<NonAARRectsVertexProvider>(std::move(array), hasColor,
                                                    std::move(strokeArray),
                                                    allocator->addReference(),
                                                    std::move(colorSpace));
}

NonAARRectsVertexProvider::NonAARRectsVertexProvider(PlacementArray<RRectRecord>&& rects,
                                                     bool hasColor,
                                                     PlacementArray<Stroke>&& strokes,
                                                     std::shared_ptr<BlockAllocator> reference,
                                                     std::shared_ptr<ColorSpace> colorSpace)
    : VertexProvider(std::move(reference)), rects(std::move(rects)), strokes(std::move(strokes)),
      _dstColorSpace(std::move(colorSpace)) {
  bitFields.hasColor = hasColor;
  bitFields.hasStroke = !this->strokes.empty();
}

size_t NonAARRectsVertexProvider::vertexCount() const {
  // Each vertex has:
  // - position (2 floats)
  // - localCoord (2 floats)
  // - radii (2 floats)
  // - rectBounds (4 floats)
  // Total: 10 floats per vertex
  // Optional: strokeWidth (2 floats) for stroke mode
  // Optional: color (1 float)
  size_t floatsPerVertex = 10;
  if (bitFields.hasStroke) {
    floatsPerVertex += 2;
  }
  if (bitFields.hasColor) {
    floatsPerVertex += 1;
  }
  // 4 vertices per RRect
  return rects.size() * 4 * floatsPerVertex;
}

void NonAARRectsVertexProvider::getVertices(float* vertices) const {
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
    auto rect = rRect.rect;
    float compressedColor = 0.f;
    if (bitFields.hasColor) {
      uint32_t uintColor = ToUintPMColor(record->color, steps.get());
      compressedColor = *reinterpret_cast<float*>(&uintColor);
    }

    auto scales = viewMatrix.getAxisScales();
    auto stroke = strokes.size() > currentIndex ? strokes[currentIndex].get() : nullptr;

    float xRadii = rRect.radii.x;
    float yRadii = rRect.radii.y;
    float halfStrokeX = 0.0f;
    float halfStrokeY = 0.0f;

    if (stroke) {
      // For hairline stroke (width == 0), use 1 pixel width in device space.
      auto strokeWidth = stroke->width > 0.0f ? stroke->width : 1.0f / std::max(scales.x, scales.y);
      halfStrokeX = 0.5f * strokeWidth;
      halfStrokeY = 0.5f * strokeWidth;
      // Expand rect bounds by half stroke width
      rect.outset(halfStrokeX, halfStrokeY);
      // Outer radii increase by half stroke width
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
      float devX = viewMatrix.getScaleX() * localX + viewMatrix.getSkewX() * localY +
                   viewMatrix.getTranslateX();
      float devY = viewMatrix.getSkewY() * localX + viewMatrix.getScaleY() * localY +
                   viewMatrix.getTranslateY();

      // position (2 floats)
      vertices[index++] = devX;
      vertices[index++] = devY;

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

      // strokeWidth (2 floats) - only for stroke mode
      if (bitFields.hasStroke) {
        vertices[index++] = halfStrokeX;
        vertices[index++] = halfStrokeY;
      }

      // Optional color
      if (bitFields.hasColor) {
        vertices[index++] = compressedColor;
      }
    }
    currentIndex++;
  }
}
}  // namespace tgfx
