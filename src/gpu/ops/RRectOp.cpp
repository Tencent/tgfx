/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "RRectOp.h"
#include "gpu/Gpu.h"
#include "gpu/GpuBuffer.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "tgfx/utils/Buffer.h"
#include "utils/MathExtra.h"

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

// clang-format off
static const uint16_t gOverstrokeRRectIndices[] = {
  // overstroke quads
  // we place this at the beginning so that we can skip these indices when rendering normally
  16, 17, 19, 16, 19, 18,
  19, 17, 23, 19, 23, 21,
  21, 23, 22, 21, 22, 20,
  22, 16, 18, 22, 18, 20,

  // corners
  0, 1, 5, 0, 5, 4,
  2, 3, 7, 2, 7, 6,
  8, 9, 13, 8, 13, 12,
  10, 11, 15, 10, 15, 14,

  // edges
  1, 2, 6, 1, 6, 5,
  4, 5, 9, 4, 9, 8,
  6, 7, 11, 6, 11, 10,
  9, 10, 14, 9, 14, 13,

  // center
  // we place this at the end so that we can ignore these indices when not rendering as filled
  5, 6, 10, 5, 10, 9,
};
// clang-format on

static constexpr int kOverstrokeIndicesCount = 6 * 4;
static constexpr int kCornerIndicesCount = 6 * 4;
static constexpr int kEdgeIndicesCount = 6 * 4;
static constexpr int kCenterIndicesCount = 6;

// fill and standard stroke indices skip the overstroke "ring"
static const uint16_t* gStandardRRectIndices = gOverstrokeRRectIndices + kOverstrokeIndicesCount;

// overstroke count is arraysize minus the center indices
// static constexpr int kIndicesPerOverstrokeRRect =
//    kOverstrokeIndicesCount + kCornerIndicesCount + kEdgeIndicesCount;
// fill count skips overstroke indices and includes center
static constexpr size_t kIndicesPerFillRRect =
    kCornerIndicesCount + kEdgeIndicesCount + kCenterIndicesCount;
// stroke count is fill count minus center indices
// static constexpr int kIndicesPerStrokeRRect = kCornerIndicesCount + kEdgeIndicesCount;

class RRectPaint {
 public:
  RRectPaint(Color color, float innerXRadius, float innerYRadius, const RRect& rRect,
             const Matrix& viewMatrix)
      : color(color), innerXRadius(innerXRadius), innerYRadius(innerYRadius), rRect(rRect),
        viewMatrix(viewMatrix) {
  }

  Color color;
  float innerXRadius;
  float innerYRadius;
  RRect rRect;
  Matrix viewMatrix;
};

void WriteColor(float* vertices, int& index, const Color& color) {
  vertices[index++] = color.red;
  vertices[index++] = color.green;
  vertices[index++] = color.blue;
  vertices[index++] = color.alpha;
}

class RRectVerticesProvider : public DataProvider {
 public:
  RRectVerticesProvider(std::vector<std::shared_ptr<RRectPaint>> rRectPaints, AAType aaType,
                        bool useScale)
      : rRectPaints(std::move(rRectPaints)), aaType(aaType), useScale(useScale) {
  }

  std::shared_ptr<Data> getData() const override {
    auto floatCount = rRectPaints.size() * 4 * 48;
    if (useScale) {
      floatCount += rRectPaints.size() * 4 * 4;
    }
    Buffer buffer(floatCount * sizeof(float));
    auto vertices = reinterpret_cast<float*>(buffer.data());
    auto index = 0;
    for (auto& rRectPaint : rRectPaints) {
      auto& viewMatrix = rRectPaint->viewMatrix;
      auto& rRect = rRectPaint->rRect;
      auto& color = rRectPaint->color;
      auto& innerXRadius = rRectPaint->innerXRadius;
      auto& innerYRadius = rRectPaint->innerYRadius;
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
      if (innerXRadius > 0) {
        reciprocalRadii[2] = 1.f / innerXRadius;
      }
      if (innerYRadius > 0) {
        reciprocalRadii[3] = 1.f / innerYRadius;
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
      // For filled rRectPaints we map a unit circle in the vertex attributes rather than
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
        WriteColor(vertices, index, color);
        vertices[index++] = xMaxOffset;
        vertices[index++] = yOuterOffsets[i];
        if (useScale) {
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
        WriteColor(vertices, index, color);
        vertices[index++] = FLOAT_NEARLY_ZERO;
        vertices[index++] = yOuterOffsets[i];
        if (useScale) {
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
        WriteColor(vertices, index, color);
        vertices[index++] = FLOAT_NEARLY_ZERO;
        vertices[index++] = yOuterOffsets[i];
        if (useScale) {
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
        WriteColor(vertices, index, color);
        vertices[index++] = xMaxOffset;
        vertices[index++] = yOuterOffsets[i];
        if (useScale) {
          vertices[index++] = maxRadius;
        }
        vertices[index++] = reciprocalRadii[0];
        vertices[index++] = reciprocalRadii[1];
        vertices[index++] = reciprocalRadii[2];
        vertices[index++] = reciprocalRadii[3];
      }
    }
    return buffer.release();
  }

 private:
  std::vector<std::shared_ptr<RRectPaint>> rRectPaints;
  AAType aaType = AAType::None;
  bool useScale = false;
};

class RRectIndicesProvider : public DataProvider {
 public:
  explicit RRectIndicesProvider(std::vector<std::shared_ptr<RRectPaint>> rRectPaints)
      : rRectPaints(std::move(rRectPaints)) {
  }

  std::shared_ptr<Data> getData() const override {
    auto bufferSize = rRectPaints.size() * kIndicesPerFillRRect * sizeof(uint16_t);
    Buffer buffer(bufferSize);
    auto indices = reinterpret_cast<uint16_t*>(buffer.data());
    int index = 0;
    for (size_t i = 0; i < rRectPaints.size(); ++i) {
      auto offset = static_cast<uint16_t>(i * 16);
      for (size_t j = 0; j < kIndicesPerFillRRect; ++j) {
        indices[index++] = gStandardRRectIndices[j] + offset;
      }
    }
    return buffer.release();
  }

 private:
  std::vector<std::shared_ptr<RRectPaint>> rRectPaints = {};
};

std::unique_ptr<RRectOp> RRectOp::Make(Color color, const RRect& rRect, const Matrix& viewMatrix) {
  Matrix matrix = Matrix::I();
  if (!viewMatrix.invert(&matrix)) {
    return nullptr;
  }
  if (/*!isStrokeOnly && */ 0.5f <= rRect.radii.x && 0.5f <= rRect.radii.y) {
    return std::unique_ptr<RRectOp>(new RRectOp(color, rRect, viewMatrix, matrix));
  }
  return nullptr;
}

RRectOp::RRectOp(Color color, const RRect& rRect, const Matrix& viewMatrix,
                 const Matrix& localMatrix)
    : DrawOp(ClassID()), localMatrix(localMatrix) {
  setTransformedBounds(rRect.rect, viewMatrix);
  auto rRectPaint = std::make_shared<RRectPaint>(color, 0.f, 0.f, rRect, viewMatrix);
  rRectPaints.push_back(std::move(rRectPaint));
}

bool RRectOp::onCombineIfPossible(Op* op) {
  if (rRectPaints.size() >= MaxNumRRects) {
    return false;
  }

  if (!DrawOp::onCombineIfPossible(op)) {
    return false;
  }
  auto* that = static_cast<RRectOp*>(op);
  if (localMatrix != that->localMatrix) {
    return false;
  }
  rRectPaints.insert(rRectPaints.end(), that->rRectPaints.begin(), that->rRectPaints.end());
  return true;
}

static bool UseScale(Context* context) {
  return !context->caps()->floatIs32Bits;
}

void RRectOp::prepare(Context* context) {
  auto useScale = UseScale(context);
  auto vertexData = std::make_shared<RRectVerticesProvider>(rRectPaints, aa, useScale);
  vertexBufferProxy = GpuBufferProxy::MakeFrom(context, std::move(vertexData), BufferType::Vertex);
  auto indexData = std::make_shared<RRectIndicesProvider>(rRectPaints);
  indexBufferProxy = GpuBufferProxy::MakeFrom(context, std::move(indexData), BufferType::Index);
}

void RRectOp::execute(RenderPass* renderPass) {
  if (indexBufferProxy == nullptr || vertexBufferProxy == nullptr) {
    return;
  }
  auto vertexBuffer = vertexBufferProxy->getBuffer();
  auto indexBuffer = indexBufferProxy->getBuffer();
  if (vertexBuffer == nullptr || indexBuffer == nullptr) {
    return;
  }
  auto pipeline = createPipeline(
      renderPass, EllipseGeometryProcessor::Make(renderPass->renderTarget()->width(),
                                                 renderPass->renderTarget()->height(), false,
                                                 UseScale(renderPass->getContext()), localMatrix));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(indexBuffer, vertexBuffer);
  renderPass->drawIndexed(PrimitiveType::Triangles, 0, rRectPaints.size() * kIndicesPerFillRRect);
}
}  // namespace tgfx
