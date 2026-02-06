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

#include "FillRRectsVertexProvider.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
// Vertex layout for FillRRectOp:
// Each vertex contains:
// - corner_and_radius_outsets (float4): xy = corner position, zw = radius outset
// - aa_bloat_and_coverage (float4): xy = AA bloat direction, z = coverage, w = is_linear_coverage
// - skew matrix (float4): scaleX, skewX, skewY, scaleY
// - translate (float2): transX, transY
// - radii (float2): same for all corners (simple RRect)
// - color (float, optional): Compressed premultiplied color

// The coverage geometry consists of an inset octagon with solid coverage, surrounded by linear
// coverage ramps on the horizontal and vertical edges, and "arc coverage" pieces on the diagonal
// edges.

// This is the offset (when multiplied by radii) from the corners of a bounding box to the vertices
// of its inscribed octagon. We draw the outside portion of arcs with quarter-octagons rather than
// rectangles.
static constexpr float Root2Over2 = 0.707106781f;  // sqrt(2) / 2
static constexpr float OctoOffset = 1.0f / (1.0f + Root2Over2);

// The vertex data for a single round rect in normalized [-1, +1] space.
// 40 vertices total: 8 inset, 8 outset, 24 corner
struct CoverageVertex {
  float corner[2];
  float radiusOutset[2];
  float aaBloatDirection[2];
  float coverage;
  float isLinearCoverage;
};

// clang-format off
static constexpr CoverageVertex VertexData[] = {
    // Left inset edge.
    {{-1,+1},  {0,-1},  {+1,0},  1,  1},
    {{-1,-1},  {0,+1},  {+1,0},  1,  1},

    // Top inset edge.
    {{-1,-1},  {+1,0},  {0,+1},  1,  1},
    {{+1,-1},  {-1,0},  {0,+1},  1,  1},

    // Right inset edge.
    {{+1,-1},  {0,+1},  {-1,0},  1,  1},
    {{+1,+1},  {0,-1},  {-1,0},  1,  1},

    // Bottom inset edge.
    {{+1,+1},  {-1,0},  {0,-1},  1,  1},
    {{-1,+1},  {+1,0},  {0,-1},  1,  1},


    // Left outset edge.
    {{-1,+1},  {0,-1},  {-1,0},  0,  1},
    {{-1,-1},  {0,+1},  {-1,0},  0,  1},

    // Top outset edge.
    {{-1,-1},  {+1,0},  {0,-1},  0,  1},
    {{+1,-1},  {-1,0},  {0,-1},  0,  1},

    // Right outset edge.
    {{+1,-1},  {0,+1},  {+1,0},  0,  1},
    {{+1,+1},  {0,-1},  {+1,0},  0,  1},

    // Bottom outset edge.
    {{+1,+1},  {-1,0},  {0,+1},  0,  1},
    {{-1,+1},  {+1,0},  {0,+1},  0,  1},


    // Top-left corner.
    {{-1,-1},  { 0,+1},  {-1, 0},  0,  0},
    {{-1,-1},  { 0,+1},  {+1, 0},  1,  0},
    {{-1,-1},  {+1, 0},  { 0,+1},  1,  0},
    {{-1,-1},  {+1, 0},  { 0,-1},  0,  0},
    {{-1,-1},  {+OctoOffset,0},  {-1,-1},  0,  0},
    {{-1,-1},  {0,+OctoOffset},  {-1,-1},  0,  0},

    // Top-right corner.
    {{+1,-1},  {-1, 0},  { 0,-1},  0,  0},
    {{+1,-1},  {-1, 0},  { 0,+1},  1,  0},
    {{+1,-1},  { 0,+1},  {-1, 0},  1,  0},
    {{+1,-1},  { 0,+1},  {+1, 0},  0,  0},
    {{+1,-1},  {0,+OctoOffset},  {+1,-1},  0,  0},
    {{+1,-1},  {-OctoOffset,0},  {+1,-1},  0,  0},

    // Bottom-right corner.
    {{+1,+1},  { 0,-1},  {+1, 0},  0,  0},
    {{+1,+1},  { 0,-1},  {-1, 0},  1,  0},
    {{+1,+1},  {-1, 0},  { 0,-1},  1,  0},
    {{+1,+1},  {-1, 0},  { 0,+1},  0,  0},
    {{+1,+1},  {-OctoOffset,0},  {+1,+1},  0,  0},
    {{+1,+1},  {0,-OctoOffset},  {+1,+1},  0,  0},

    // Bottom-left corner.
    {{-1,+1},  {+1, 0},  { 0,+1},  0,  0},
    {{-1,+1},  {+1, 0},  { 0,-1},  1,  0},
    {{-1,+1},  { 0,-1},  {+1, 0},  1,  0},
    {{-1,+1},  { 0,-1},  {-1, 0},  0,  0},
    {{-1,+1},  {0,-OctoOffset},  {-1,+1},  0,  0},
    {{-1,+1},  {+OctoOffset,0},  {-1,+1},  0,  0}};
// clang-format on

static constexpr size_t VertexCount = std::size(VertexData);
static_assert(VertexCount == 40, "FillRRectOp vertex count must be 40");

PlacementPtr<FillRRectsVertexProvider> FillRRectsVertexProvider::MakeFrom(
    BlockAllocator* allocator, std::vector<PlacementPtr<RRectRecord>>&& rects, AAType aaType,
    std::shared_ptr<ColorSpace> colorSpace) {
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
  return allocator->make<FillRRectsVertexProvider>(std::move(array), aaType, hasColor,
                                                   allocator->addReference(),
                                                   std::move(colorSpace));
}

FillRRectsVertexProvider::FillRRectsVertexProvider(PlacementArray<RRectRecord>&& rects,
                                                   AAType aaType, bool hasColor,
                                                   std::shared_ptr<BlockAllocator> reference,
                                                   std::shared_ptr<ColorSpace> colorSpace)
    : VertexProvider(std::move(reference)), rects(std::move(rects)),
      _dstColorSpace(std::move(colorSpace)) {
  bitFields.aaType = static_cast<uint8_t>(aaType);
  bitFields.hasColor = hasColor;
}

size_t FillRRectsVertexProvider::vertexCount() const {
  // Each vertex has:
  // - corner_and_radius_outsets (4 floats)
  // - aa_bloat_and_coverage (4 floats)
  // - skew matrix (4 floats)
  // - translate (2 floats)
  // - radii (2 floats)
  // Total: 16 floats per vertex
  // Optional: color (1 float)
  size_t floatsPerVertex = 16;
  if (bitFields.hasColor) {
    floatsPerVertex += 1;
  }
  return rects.size() * VertexCount * floatsPerVertex;
}

void FillRRectsVertexProvider::getVertices(float* vertices) const {
  size_t index = 0;
  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (bitFields.hasColor && NeedConvertColorSpace(ColorSpace::SRGB(), _dstColorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                               _dstColorSpace.get(), AlphaType::Premultiplied);
  }

  for (auto& record : rects) {
    auto viewMatrix = record->viewMatrix;
    auto rRect = record->rRect;
    auto rect = rRect.rect;
    float compressedColor = 0.f;
    if (bitFields.hasColor) {
      uint32_t uintColor = ToUintPMColor(record->color, steps.get());
      compressedColor = *reinterpret_cast<float*>(&uintColor);
    }

    // Get the bounds
    auto left = rect.left;
    auto top = rect.top;
    auto right = rect.right;
    auto bottom = rect.bottom;

    // Produce a matrix that draws the round rect from normalized [-1, -1, +1, +1] space.
    // Unmap the normalized rect [-1, -1, +1, +1] back to [l, t, r, b].
    Matrix m = Matrix::MakeScale((right - left) / 2.0f, (bottom - top) / 2.0f);
    m.postTranslate((left + right) / 2.0f, (top + bottom) / 2.0f);
    // Map to device space.
    m.postConcat(viewMatrix);

    // Convert the radii to [-1, -1, +1, +1] space.
    // For simple RRect, all corners have the same radii.
    float xRadii = rRect.radii.x * 2.0f / (right - left);
    float yRadii = rRect.radii.y * 2.0f / (bottom - top);

    // Get the skew matrix components
    float scaleX = m.getScaleX();
    float skewX = m.getSkewX();
    float skewY = m.getSkewY();
    float scaleY = m.getScaleY();
    float transX = m.getTranslateX();
    float transY = m.getTranslateY();

    // Write vertex data for each of the 40 vertices
    for (size_t v = 0; v < VertexCount; ++v) {
      const auto& vtx = VertexData[v];

      // corner_and_radius_outsets (4 floats)
      vertices[index++] = vtx.corner[0];
      vertices[index++] = vtx.corner[1];
      vertices[index++] = vtx.radiusOutset[0];
      vertices[index++] = vtx.radiusOutset[1];

      // aa_bloat_and_coverage (4 floats)
      vertices[index++] = vtx.aaBloatDirection[0];
      vertices[index++] = vtx.aaBloatDirection[1];
      vertices[index++] = vtx.coverage;
      vertices[index++] = vtx.isLinearCoverage;

      // skew matrix (4 floats)
      vertices[index++] = scaleX;
      vertices[index++] = skewX;
      vertices[index++] = skewY;
      vertices[index++] = scaleY;

      // translate (2 floats)
      vertices[index++] = transX;
      vertices[index++] = transY;

      // radii (2 floats)
      vertices[index++] = xRadii;
      vertices[index++] = yRadii;

      // Optional color
      if (bitFields.hasColor) {
        vertices[index++] = compressedColor;
      }
    }
  }
}
}  // namespace tgfx
