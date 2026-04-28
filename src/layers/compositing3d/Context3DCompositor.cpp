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
#include <algorithm>
#include "BspTree.h"
#include "core/images/TextureImage.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/QuadRecord.h"
#include "gpu/QuadsVertexProvider.h"
#include "gpu/ops/Quads3DDrawOp.h"
#include "gpu/processors/TextureEffect.h"
#include "layers/BackgroundContext.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

// Tolerance for determining if a vertex lies on the original rectangle's edge.
static constexpr float AA_EPSILON = 0.01f;

// Flags indicating the edges of a rectangle.
static constexpr unsigned RECT_EDGE_LEFT = 0b1000;
static constexpr unsigned RECT_EDGE_TOP = 0b0001;
static constexpr unsigned RECT_EDGE_RIGHT = 0b0010;
static constexpr unsigned RECT_EDGE_BOTTOM = 0b0100;

static inline std::shared_ptr<Image> ToImageWithOffset(std::shared_ptr<Picture> picture,
                                                       std::shared_ptr<ColorSpace> colorSpace,
                                                       Point* offset,
                                                       const Rect* imageBounds = nullptr) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = imageBounds ? *imageBounds : picture->getBounds();
  bounds.roundOut();
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  if (offset != nullptr) {
    offset->x = bounds.left;
    offset->y = bounds.top;
  }
  return Image::MakeFrom(std::move(picture), static_cast<int>(bounds.width()),
                         static_cast<int>(bounds.height()), &matrix, std::move(colorSpace));
}

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
static unsigned GetQuadAAFlags(const Quad& quad, const Rect& rect) {
  const unsigned p0 = DeterminePointOnRectEdge(quad.point(0), rect);
  const unsigned p1 = DeterminePointOnRectEdge(quad.point(1), rect);
  const unsigned p2 = DeterminePointOnRectEdge(quad.point(2), rect);
  const unsigned p3 = DeterminePointOnRectEdge(quad.point(3), rect);

  unsigned aaFlags = QUAD_AA_FLAG_NONE;
  if (IsExteriorEdge(p0, p1)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_0;
  }
  if (IsExteriorEdge(p1, p3)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_1;
  }
  if (IsExteriorEdge(p2, p0)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_2;
  }
  if (IsExteriorEdge(p3, p2)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_3;
  }
  return aaFlags;
}

/**
 * Returns the quad to draw and its AA flags based on whether it's a sub-quad or the original rect.
 */
static std::pair<Quad, unsigned> GetQuadAndAAFlags(const Rect& originalRect, AAType aaType,
                                                   const Quad* subQuad) {
  if (subQuad != nullptr) {
    auto aaFlags = QUAD_AA_FLAG_NONE;
    if (aaType == AAType::Coverage) {
      aaFlags = GetQuadAAFlags(*subQuad, originalRect);
    }
    return {*subQuad, aaFlags};
  }
  auto quad = Quad::MakeFrom(originalRect);
  auto aaFlags = (aaType == AAType::Coverage) ? QUAD_AA_FLAG_ALL : QUAD_AA_FLAG_NONE;
  return {quad, aaFlags};
}

Context3DCompositor::Context3DCompositor(const Context& context, const Rect& renderRect,
                                         float contentScale, std::shared_ptr<ColorSpace> colorSpace,
                                         std::shared_ptr<BackgroundContext> backgroundContext)
    : _renderRect(renderRect), _contentScale(contentScale), _colorSpace(std::move(colorSpace)),
      _backgroundContext(std::move(backgroundContext)) {
  _targetColorProxy = context.proxyProvider()->createRenderTargetProxy(
      {}, static_cast<int>(renderRect.width()), static_cast<int>(renderRect.height()),
      PixelFormat::RGBA_8888);
  DEBUG_ASSERT(_targetColorProxy != nullptr);
}

void Context3DCompositor::addDrawRect(std::unique_ptr<DrawableRect> drawRect) {
  auto rectPtr = drawRect.get();
  _drawRects.push_back(std::move(drawRect));
  auto polygon = std::make_unique<DrawPolygon3D>(rectPtr);
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
                                    const std::vector<Quad>& subQuads) {
  DEBUG_ASSERT(_targetColorProxy != nullptr);
  auto context = _targetColorProxy->getContext();
  DEBUG_ASSERT(context != nullptr);
  auto drawRect = polygon->drawRect();
  auto aaType = GetAAType(_targetColorProxy->sampleCount(), drawRect->antiAlias);
  const auto& image = drawRect->image;
  auto srcW = static_cast<float>(image->width());
  auto srcH = static_cast<float>(image->height());
  Rect originalRect = Rect::MakeWH(srcW, srcH);

  auto allocator = context->drawingAllocator();
  // Layer alpha is already baked into the image; use opaque white to skip modulation.
  Color vertexColor = Color::White();
  auto matrix = drawRect->matrix.asMatrix();
  std::vector<PlacementPtr<QuadRecord>> quadRecords;
  if (subQuads.empty()) {
    auto [quad, aaFlags] = GetQuadAndAAFlags(originalRect, aaType, nullptr);
    quadRecords.push_back(allocator->make<QuadRecord>(quad, aaFlags, vertexColor, matrix));
  } else {
    for (const auto& subQuad : subQuads) {
      auto [quad, aaFlags] = GetQuadAndAAFlags(originalRect, aaType, &subQuad);
      quadRecords.push_back(allocator->make<QuadRecord>(quad, aaFlags, vertexColor, matrix));
    }
  }

  auto vertexProvider =
      QuadsVertexProvider::MakeFrom(allocator, std::move(quadRecords), aaType, _colorSpace);
  auto drawOp = Quads3DDrawOp::Make(context, std::move(vertexProvider), 0);

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
    // Sort polygons by (depth, sequenceIndex) to ensure correct paint order in BSP tree.
    // TODO: Support pre-order traversal of layers to avoid the performance cost of sorting.
    std::sort(_polygons.begin(), _polygons.end(), DrawPolygon3DOrder);
    BspTree bspTree(std::move(_polygons));
    _polygons.clear();
    bspTree.traverseBackToFront([this](const DrawPolygon3D* polygon) {
      drawBackgroundStyles(polygon);
      drawPolygon(polygon);
      syncToBackgroundContext(polygon);
    });
  }

  auto opArray = context->drawingAllocator()->makeArray(std::move(_drawOps));
  context->drawingManager()->addOpsRenderTask(_targetColorProxy, std::move(opArray),
                                              PMColor::Transparent());
  auto image = TextureImage::Wrap(_targetColorProxy->asTextureProxy(), _colorSpace);
  _targetColorProxy = nullptr;
  return image;
}

std::shared_ptr<Image> Context3DCompositor::getBackgroundImage(const DrawableRect& drawRect,
                                                               Point* offset) const {
  DEBUG_ASSERT(drawRect.background.has_value());
  const auto& background = *drawRect.background;
  DEBUG_ASSERT(background.mask != nullptr);
  Matrix globalToLocalMatrix = {};
  if (!background.globalMatrix.invert(&globalToLocalMatrix)) {
    return nullptr;
  }
  auto backgroundBounds = Rect::MakeXYWH(background.maskOffset.x, background.maskOffset.y,
                                         static_cast<float>(background.mask->width()),
                                         static_cast<float>(background.mask->height()));
  // Some styles sample beyond the region they paint, so union each style's required sampling area.
  auto sampleBounds = backgroundBounds;
  for (const auto& style : background.styles) {
    DEBUG_ASSERT(style != nullptr);
    sampleBounds.join(style->filterBackground(backgroundBounds, _contentScale));
  }
  PictureRecorder recorder = {};
  auto canvas = recorder.beginRecording();
  // Draw the background only within the sampling region.
  canvas->clipRect(sampleBounds);
  canvas->concat(globalToLocalMatrix);
  canvas->concat(_backgroundContext->backgroundMatrix());
  canvas->drawImage(_backgroundContext->getBackgroundImage());
  return ToImageWithOffset(recorder.finishRecordingAsPicture(), _colorSpace, offset);
}

void Context3DCompositor::drawBackgroundStyles(const DrawPolygon3D* polygon) {
  if (_backgroundContext == nullptr) {
    return;
  }
  auto drawRect = polygon->drawRect();
  if (!drawRect->background.has_value()) {
    return;
  }
  const auto& background = *drawRect->background;
  // The offset of the background image in the Rect local space.
  Point bgImageOffset = {};
  const auto bgImage = getBackgroundImage(*drawRect, &bgImageOffset);
  if (bgImage == nullptr) {
    return;
  }

  for (const auto& style : background.styles) {
    DEBUG_ASSERT(style != nullptr);
    DEBUG_ASSERT(style->extraSourceType() == LayerStyleExtraSourceType::Background);
    drawBackgroundStyle(polygon, *style, background.mask, background.maskOffset, bgImage,
                        bgImageOffset);
  }
}

void Context3DCompositor::drawBackgroundStyle(const DrawPolygon3D* polygon, LayerStyle& style,
                                              const std::shared_ptr<Image>& mask,
                                              const Point& maskOffset,
                                              const std::shared_ptr<Image>& bgImage,
                                              const Point& bgOffset) {
  auto drawRect = polygon->drawRect();
  DEBUG_ASSERT(drawRect->image != nullptr);
  auto imageWidth = static_cast<float>(drawRect->image->width());
  auto imageHeight = static_cast<float>(drawRect->image->height());
  PictureRecorder recorder = {};
  auto pictureCanvas = recorder.beginRecording();
  // The style result will replace the rect's fill texture, so keep its bounds equal to the
  // original image.
  pictureCanvas->clipRect(Rect::MakeWH(imageWidth, imageHeight));
  // Align the canvas origin with the mask's position in the rect local space.
  pictureCanvas->translate(maskOffset.x, maskOffset.y);
  auto relativeOffset = bgOffset - maskOffset;
  style.drawWithExtraSource(pictureCanvas, mask, _contentScale, bgImage, relativeOffset, 1.0f);
  // Extract the result image aligned with the original image (same origin, same size), so the
  // polygon's matrix and points can be reused without adjustment.
  auto imageBounds = Rect::MakeWH(imageWidth, imageHeight);
  auto resultImage =
      ToImageWithOffset(recorder.finishRecordingAsPicture(), _colorSpace, nullptr, &imageBounds);
  if (resultImage == nullptr) {
    return;
  }
  // Create a variant of the rect with the style result baked in. The background is cleared to
  // reflect that this rect no longer has pending background styles.
  auto resultRect = std::make_unique<DrawableRect>(*drawRect);
  resultRect->image = std::move(resultImage);
  resultRect->background = std::nullopt;
  auto resultRectPtr = resultRect.get();
  _drawRects.push_back(std::move(resultRect));
  auto resultPolygon = polygon->makeVariant(resultRectPtr);
  drawPolygon(&resultPolygon);
}

void Context3DCompositor::syncToBackgroundContext(const DrawPolygon3D* polygon) const {
  if (_backgroundContext == nullptr) {
    return;
  }
  auto drawRect = polygon->drawRect();
  auto bgCanvas = _backgroundContext->getCanvas();
  AutoCanvasRestore autoRestore(bgCanvas);
  // The polygon's matrix is relative to the 3D context's coordinate system. Translate to the
  // global canvas coordinate system used by the background context.
  bgCanvas->translate(_renderRect.x(), _renderRect.y());
  bgCanvas->concat(drawRect->matrix.asMatrix());
  if (polygon->isSplit()) {
    // Clip to the split fragment so only its portion is drawn, not the entire layer image.
    auto inverseMatrix = Matrix3D::I();
    if (!drawRect->matrix.invert(&inverseMatrix)) {
      DEBUG_ASSERT(false);
      return;
    }
    Path clipPath = {};
    for (const auto& point : polygon->points()) {
      auto local = inverseMatrix.mapPoint(point);
      if (clipPath.isEmpty()) {
        clipPath.moveTo(local.x, local.y);
      } else {
        clipPath.lineTo(local.x, local.y);
      }
    }
    clipPath.close();
    bgCanvas->clipPath(clipPath);
  }
  bgCanvas->drawImage(drawRect->image);
}

}  // namespace tgfx
