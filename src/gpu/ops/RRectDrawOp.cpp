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

#include "RRectDrawOp.h"
#include "core/DataSource.h"
#include "core/utils/MathExtra.h"
#include "gpu/GpuBuffer.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ResourceProvider.h"
#include "gpu/VertexProvider.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "tgfx/core/RenderFlags.h"

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

static void WriteUByte4Color(float* vertices, int& index, const Color& color) {
  auto bytes = reinterpret_cast<uint8_t*>(&vertices[index++]);
  bytes[0] = static_cast<uint8_t>(color.red * 255);
  bytes[1] = static_cast<uint8_t>(color.green * 255);
  bytes[2] = static_cast<uint8_t>(color.blue * 255);
  bytes[3] = static_cast<uint8_t>(color.alpha * 255);
}

class RRectVertexProvider : public VertexProvider {
 public:
  RRectVertexProvider(std::vector<PlacementPtr<RRectPaint>> rRectPaints, AAType aaType,
                      bool useScale)
      : rRectPaints(std::move(rRectPaints)), aaType(aaType), useScale(useScale) {
  }

  size_t vertexCount() const override {
    auto floatCount = rRectPaints.size() * 4 * 36;
    if (useScale) {
      floatCount += rRectPaints.size() * 4 * 4;
    }
    return floatCount;
  }

  void getVertices(float* vertices) const override {
    auto index = 0;
    for (auto& rRectPaint : rRectPaints) {
      auto viewMatrix = rRectPaint->viewMatrix;
      auto rRect = rRectPaint->rRect;
      auto& color = rRectPaint->color;
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
        WriteUByte4Color(vertices, index, color);
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
        WriteUByte4Color(vertices, index, color);
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
        WriteUByte4Color(vertices, index, color);
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
        WriteUByte4Color(vertices, index, color);
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
  }

 private:
  std::vector<PlacementPtr<RRectPaint>> rRectPaints = {};
  AAType aaType = AAType::None;
  bool useScale = false;
};

static bool UseScale(Context* context) {
  return !context->caps()->floatIs32Bits;
}

PlacementPtr<RRectDrawOp> RRectDrawOp::Make(Context* context,
                                            std::vector<PlacementPtr<RRectPaint>> rects,
                                            AAType aaType, uint32_t renderFlags) {
  if (rects.empty()) {
    return nullptr;
  }
  auto rectSize = rects.size();
  auto drawOp = context->drawingBuffer()->make<RRectDrawOp>(aaType, rectSize);
  drawOp->indexBufferProxy = context->resourceProvider()->rRectIndexBuffer();
  auto useScale = UseScale(context);
  auto vertexProvider = std::make_unique<RRectVertexProvider>(std::move(rects), aaType, useScale);
  if (rectSize <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  auto sharedVertexBuffer =
      context->proxyProvider()->createSharedVertexBuffer(std::move(vertexProvider), renderFlags);
  drawOp->vertexBufferProxy = sharedVertexBuffer.first;
  drawOp->vertexBufferOffset = sharedVertexBuffer.second;
  return drawOp;
}

RRectDrawOp::RRectDrawOp(AAType aaType, size_t rectCount) : DrawOp(aaType), rectCount(rectCount) {
}

void RRectDrawOp::execute(RenderPass* renderPass) {
  if (indexBufferProxy == nullptr) {
    return;
  }
  auto indexBuffer = indexBufferProxy->getBuffer();
  if (indexBuffer == nullptr) {
    return;
  }
  std::shared_ptr<GpuBuffer> vertexBuffer =
      vertexBufferProxy ? vertexBufferProxy->getBuffer() : nullptr;
  if (vertexBuffer == nullptr) {
    return;
  }
  auto renderTarget = renderPass->renderTarget();
  auto drawingBuffer = renderPass->getContext()->drawingBuffer();
  auto gp =
      EllipseGeometryProcessor::Make(drawingBuffer, renderTarget->width(), renderTarget->height(),
                                     false, UseScale(renderPass->getContext()));
  auto pipeline = createPipeline(renderPass, std::move(gp));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(indexBuffer, vertexBuffer, vertexBufferOffset);
  auto numIndicesPerRRect = ResourceProvider::NumIndicesPerRRect();
  renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerRRect);
}
}  // namespace tgfx
