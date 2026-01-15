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

#include "Context3DCompositor.h"
#include <cmath>
#include "BspTree.h"
#include "core/images/TextureImage.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/QuadRecord.h"
#include "gpu/QuadsVertexProvider.h"
#include "gpu/ops/Quads3DDrawOp.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

// Tolerance for determining if a vertex lies on the original rectangle's edge.
static constexpr float AA_EPSILON = 0.01f;

// Flags indicating the edges of a rectangle.
static constexpr unsigned RECT_EDGE_LEFT = 0b1000;
static constexpr unsigned RECT_EDGE_TOP = 0b0001;
static constexpr unsigned RECT_EDGE_RIGHT = 0b0010;
static constexpr unsigned RECT_EDGE_BOTTOM = 0b0100;

static inline AAType GetAAType(int sampleCount, bool antiAlias) {
  if (sampleCount > 1) {
    return AAType::MSAA;
  }
  if (antiAlias) {
    return AAType::Coverage;
  }
  return AAType::None;
}

/**
 * Determines which edges of the rect this point lies on.
 */
static unsigned DeterminePointOnRectEdge(const Point& point, const Rect& rect) {
  unsigned edges = 0;
  if (std::abs(point.x - rect.left) < AA_EPSILON) {
    edges |= RECT_EDGE_LEFT;
  }
  if (std::abs(point.x - rect.right) < AA_EPSILON) {
    edges |= RECT_EDGE_RIGHT;
  }
  if (std::abs(point.y - rect.top) < AA_EPSILON) {
    edges |= RECT_EDGE_TOP;
  }
  if (std::abs(point.y - rect.bottom) < AA_EPSILON) {
    edges |= RECT_EDGE_BOTTOM;
  }
  return edges;
}

/**
 * Returns true if both endpoints of a quad edge lie on the same edge of the original rectangle,
 * indicating that this edge is an exterior edge of the original rectangle.
 */
static bool IsExteriorEdge(unsigned cornerAEdge, unsigned cornerBEdge) {
  return (cornerAEdge & cornerBEdge) != 0;
}

/**
 * Computes per-edge AA flags for a quad. An edge needs AA if both endpoints lie on the same
 * edge of the original rect (i.e., it's an exterior edge, not a BSP split edge).
 */
static unsigned GetQuadAAFlags(const QuadCW& quad, const Rect& rect) {
  const unsigned p0 = DeterminePointOnRectEdge(quad.point(0), rect);
  const unsigned p1 = DeterminePointOnRectEdge(quad.point(1), rect);
  const unsigned p2 = DeterminePointOnRectEdge(quad.point(2), rect);
  const unsigned p3 = DeterminePointOnRectEdge(quad.point(3), rect);

  unsigned aaFlags = QUAD_AA_FLAG_NONE;
  if (IsExteriorEdge(p0, p1)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_01;
  }
  if (IsExteriorEdge(p1, p2)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_12;
  }
  if (IsExteriorEdge(p2, p3)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_23;
  }
  if (IsExteriorEdge(p3, p0)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_30;
  }
  return aaFlags;
}

/**
 * Returns the quad to draw and its AA flags based on whether it's a sub-quad or the original rect.
 */
static std::pair<QuadCW, unsigned> GetQuadAndAAFlags(const Rect& originalRect, AAType aaType,
                                                     const QuadCW* subQuad) {
  QuadCW quad;
  unsigned aaFlags = QUAD_AA_FLAG_NONE;

  if (subQuad != nullptr) {
    quad = *subQuad;
    if (aaType == AAType::Coverage) {
      aaFlags = GetQuadAAFlags(quad, originalRect);
    }
  } else {
    quad = QuadCW(Point::Make(originalRect.left, originalRect.top),
                  Point::Make(originalRect.right, originalRect.top),
                  Point::Make(originalRect.right, originalRect.bottom),
                  Point::Make(originalRect.left, originalRect.bottom));
    if (aaType == AAType::Coverage) {
      aaFlags = QUAD_AA_FLAG_ALL;
    }
  }

  return {quad, aaFlags};
}

Context3DCompositor::Context3DCompositor(const Context& context, int width, int height)
    : _width(width), _height(height) {
  _targetColorProxy =
      context.proxyProvider()->createRenderTargetProxy({}, width, height, PixelFormat::RGBA_8888);
  DEBUG_ASSERT(_targetColorProxy != nullptr);
}

void Context3DCompositor::addImage(std::shared_ptr<Image> image, const Matrix3D& matrix,
                                   float alpha, bool antiAlias) {
  auto polygon = std::make_unique<DrawPolygon3D>(std::move(image), matrix, _nextOrderIndex++, alpha,
                                                 antiAlias);
  _polygons.push_back(std::move(polygon));
}

void Context3DCompositor::drawPolygon(const DrawPolygon3D* polygon) {
  if (!polygon->isSplit()) {
    drawQuads(polygon, {});
  } else {
    drawQuads(polygon, polygon->toQuads());
  }
}

void Context3DCompositor::drawQuads(const DrawPolygon3D* polygon,
                                    const std::vector<QuadCW>& subQuads) {
  DEBUG_ASSERT(_targetColorProxy != nullptr);
  auto context = _targetColorProxy->getContext();
  DEBUG_ASSERT(context != nullptr);
  auto aaType = GetAAType(_targetColorProxy->sampleCount(), polygon->antiAlias());
  const auto& image = polygon->image();
  auto srcW = static_cast<float>(image->width());
  auto srcH = static_cast<float>(image->height());
  Rect originalRect = Rect::MakeWH(srcW, srcH);

  auto allocator = context->drawingAllocator();
  // Wrap alpha as vertex color to enable semi-transparent pixel blending.
  Color vertexColor(1, 1, 1, polygon->alpha());
  std::vector<PlacementPtr<QuadRecord>> quadRecords;
  if (subQuads.empty()) {
    auto [quad, aaFlags] = GetQuadAndAAFlags(originalRect, aaType, nullptr);
    quadRecords.push_back(allocator->make<QuadRecord>(quad, aaFlags, vertexColor));
  } else {
    for (const auto& subQuad : subQuads) {
      auto [quad, aaFlags] = GetQuadAndAAFlags(originalRect, aaType, &subQuad);
      quadRecords.push_back(allocator->make<QuadRecord>(quad, aaFlags, vertexColor));
    }
  }

  // Flatten z-axis to keep vertices at their original depth, preventing clipping space culling.
  auto matrix = polygon->matrix();
  matrix.setRow(2, {0, 0, 1, 0});
  auto widthF = static_cast<float>(_width);
  auto heightF = static_cast<float>(_height);
  // Map projected vertex coordinates from render target texture space to NDC space.
  const Vec2 ndcScale(2.0f / widthF, 2.0f / heightF);
  const Vec2 ndcOffset(-1.f, -1.f);
  auto vertexProvider = QuadsVertexProvider::MakeFrom(allocator, std::move(quadRecords), aaType);
  const Size viewportSize(static_cast<float>(_width), static_cast<float>(_height));
  const Quads3DDrawArgs drawArgs{matrix, ndcScale, ndcOffset, viewportSize};
  auto drawOp = Quads3DDrawOp::Make(context, std::move(vertexProvider), 0, drawArgs);

  const SamplingArgs samplingArgs = {TileMode::Clamp, TileMode::Clamp, {}, SrcRectConstraint::Fast};
  auto textureImage = image->makeTextureImage(context);
  if (textureImage == nullptr) {
    return;
  }
  auto sourceTextureProxy = std::static_pointer_cast<TextureImage>(textureImage)->getTextureProxy();
  /**
   * Ensure the vertex texture sampling coordinates are in the range [0, 1]. The size obtained from
   * Image is the original size, while the texture size generated by Image is the size after
   * applying DrawScale. Texture sampling requires corresponding scaling.
   */
  DEBUG_ASSERT(srcW > 0 && srcH > 0);
  auto uvMatrix = Matrix::MakeScale(static_cast<float>(sourceTextureProxy->width()) / srcW,
                                    static_cast<float>(sourceTextureProxy->height()) / srcH);
  auto fragmentProcessor =
      TextureEffect::Make(allocator, std::move(sourceTextureProxy), samplingArgs, &uvMatrix);
  drawOp->addColorFP(std::move(fragmentProcessor));
  _drawOps.emplace_back(std::move(drawOp));
}

std::shared_ptr<Image> Context3DCompositor::finish() {
  auto context = _targetColorProxy->getContext();
  DEBUG_ASSERT(context != nullptr);

  if (!_polygons.empty()) {
    BspTree bspTree(std::move(_polygons));
    _polygons.clear();
    bspTree.traverseBackToFront([this](const DrawPolygon3D* polygon) { drawPolygon(polygon); });
  }

  auto opArray = context->drawingAllocator()->makeArray(std::move(_drawOps));
  context->drawingManager()->addOpsRenderTask(_targetColorProxy, std::move(opArray),
                                              PMColor::Transparent());
  auto image = TextureImage::Wrap(_targetColorProxy->asTextureProxy(), ColorSpace::SRGB());
  _targetColorProxy = nullptr;
  return image;
}

}  // namespace tgfx
