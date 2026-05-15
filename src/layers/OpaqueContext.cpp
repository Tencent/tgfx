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

#include "OpaqueContext.h"
#include "core/CanvasState.h"
#include "core/utils/RectToRectMatrix.h"
#include "core/utils/StrokeUtils.h"
#include "layers/OpaqueThreshold.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {

class PendingOpaqueAutoReset {
 public:
  PendingOpaqueAutoReset(OpaqueContext* context, const OpaqueContext::Contour& contour,
                         const Matrix& matrix, const ClipStack& clip, Brush brush)
      : context(context), contour(contour), matrix(matrix), clip(clip), brush(std::move(brush)) {
  }

  ~PendingOpaqueAutoReset() {
    context->resetPendingContour(contour, matrix, clip, brush);
  }

 private:
  OpaqueContext* context = nullptr;
  OpaqueContext::Contour contour;
  Matrix matrix;
  ClipStack clip;
  Brush brush;
};

OpaqueContext::OpaqueContext() {
  contourBounds.reserve(3);
}

OpaqueContext::~OpaqueContext() = default;

Canvas* OpaqueContext::beginRecording() {
  if (canvas == nullptr) {
    canvas = std::unique_ptr<Canvas>(new Canvas(this));
  } else {
    canvas->resetStateStack();
  }
  pendingContour = {};
  pendingMatrix = {};
  pendingClip = {};
  pendingBrushes.clear();
  contourBounds.clear();
  pictureContext.clear();
  return canvas.get();
}

void OpaqueContext::drawFill(const Brush& brush) {
  static Contour FillContour = Contour(Contour::Type::Fill);
  drawContour(FillContour, Matrix::I(), ClipStack(), brush);
}

void OpaqueContext::drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip,
                             const Brush& brush, const Stroke* stroke) {
  drawContour(Contour(rect, stroke), matrix, clip, brush);
}

void OpaqueContext::drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                              const Brush& brush, const Stroke* stroke) {
  drawContour(Contour(rRect, stroke), matrix, clip, brush);
}

void OpaqueContext::drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                             const Brush& brush) {
  drawContour(Contour(path), matrix, clip, brush);
}

void OpaqueContext::drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix,
                              const ClipStack& clip, const Brush& brush, const Stroke* stroke) {
  drawContour(Contour(std::move(shape), stroke), matrix, clip, brush);
}

void OpaqueContext::drawMesh(std::shared_ptr<Mesh> mesh, const Matrix& matrix,
                             const ClipStack& clip, const Brush& brush) {
  auto bounds = matrix.mapRect(mesh->bounds());
  if (containContourBound(bounds)) {
    return;
  }
  flushPendingContour();
  pictureContext.drawMesh(std::move(mesh), matrix, clip, brush);
}

void OpaqueContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                              const Matrix& matrix, const ClipStack& clip, const Brush& brush) {
  auto newBrush = brush;
  newBrush.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
  drawRect(Rect::MakeWH(image->width(), image->height()), matrix, clip, newBrush, nullptr);
}

void OpaqueContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                  const Rect& dstRect, const SamplingOptions& sampling,
                                  const Matrix& matrix, const ClipStack& clip, const Brush& brush,
                                  SrcRectConstraint constraint) {
  if (constraint != SrcRectConstraint::Strict) {
    auto newMatrix = matrix;
    newMatrix.preConcat(MakeRectToRectMatrix(srcRect, dstRect));
    auto path = Path();
    path.addRect(dstRect);
    path.transform(matrix);
    auto newClip = clip;
    newClip.clip(path, false);
    auto newBrush = brush;
    newBrush.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
    drawRect(dstRect, newMatrix, newClip, newBrush, nullptr);
    return;
  }
  auto bounds = matrix.mapRect(dstRect);
  if (containContourBound(bounds)) {
    return;
  }
  flushPendingContour();
  pictureContext.drawImageRect(image, srcRect, dstRect, sampling, matrix, clip, brush, constraint);
}

void OpaqueContext::drawTextBlob(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix,
                                 const ClipStack& clip, const Brush& brush, const Stroke* stroke) {
  auto bounds = textBlob->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds, matrix);
  }
  bounds = matrix.mapRect(bounds);
  if (containContourBound(bounds)) {
    return;
  }
  flushPendingContour();
  pictureContext.drawTextBlob(textBlob, matrix, clip, brush, stroke);
}

void OpaqueContext::drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                                const ClipStack& clip) {
  picture->playback(this, matrix, clip);
}

void OpaqueContext::drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                              const Matrix& matrix, const ClipStack& clip, const Brush& brush) {
  if (brush.nothingToDraw()) {
    return;
  }
  if (!filter && !brush.maskFilter) {
    drawPicture(picture, matrix, clip);
    return;
  }
  if (!picture->hasUnboundedFill()) {
    auto bounds = picture->getBounds();
    if (filter) {
      bounds = filter->filterBounds(bounds);
    }
    bounds = matrix.mapRect(bounds);
    if (containContourBound(bounds)) {
      return;
    }
  }
  flushPendingContour();
  pictureContext.drawLayer(picture, filter, matrix, clip, brush);
}

void OpaqueContext::drawContour(const Contour& contour, const Matrix& matrix, const ClipStack& clip,
                                const Brush& brush) {
  if (canAppend(contour, matrix, clip, brush)) {
    appendFill(brush);
    return;
  }
  flushPendingContour(contour, matrix, clip, brush);
}

bool OpaqueContext::containContourBound(const Rect& bounds) const {
  return bounds.isEmpty() || std::any_of(contourBounds.begin(), contourBounds.end(),
                                         [&](const Rect& rect) { return rect.contains(bounds); });
}

Rect GetMaxOverlapRect(const Rect& rect1, const Rect& rect2) {
  auto intersect = rect1;
  if (intersect.intersect(rect2)) {
    float left = std::min(rect1.left, rect2.left);
    float top = std::min(rect1.top, rect2.top);
    float right = std::max(rect1.right, rect2.right);
    float bottom = std::max(rect1.bottom, rect2.bottom);
    auto overlap1 = Rect::MakeLTRB(intersect.left, top, intersect.right, bottom);
    auto overlap2 = Rect::MakeLTRB(left, intersect.top, right, intersect.bottom);
    return overlap1.area() > overlap2.area() ? overlap1 : overlap2;
  }
  return Rect::MakeEmpty();
}

void OpaqueContext::mergeContourBound(const Rect& bounds) {
  if (contourBounds.size() < 3) {
    contourBounds.emplace_back(bounds);
    if (contourBounds.size() == 3) {
      std::sort(contourBounds.begin(), contourBounds.end(),
                [](const Rect& a, const Rect& b) { return a.area() > b.area(); });
    }
    return;
  }
  size_t bestIdx = 0;
  float maxOverlapArea = 0;
  Rect bestOverlap = Rect::MakeEmpty();
  for (size_t i = 0; i < contourBounds.size(); ++i) {
    const auto& r = contourBounds[i];
    auto overlap = GetMaxOverlapRect(r, bounds);
    float overlapArea = overlap.area();
    if (overlapArea < bounds.area()) {
      continue;
    }
    if (overlapArea > maxOverlapArea) {
      maxOverlapArea = overlapArea;
      bestIdx = i;
      bestOverlap = overlap;
    }
  }
  if (maxOverlapArea > 0) {
    contourBounds[bestIdx] = bestOverlap;
  } else {
    if (bounds.area() > contourBounds.back().area()) {
      contourBounds.back() = bounds;
    }
  }
  std::sort(contourBounds.begin(), contourBounds.end(),
            [](const Rect& a, const Rect& b) { return a.area() > b.area(); });
}

std::shared_ptr<Picture> OpaqueContext::finishRecordingAsPicture() {
  flushPendingContour();
  return pictureContext.finishRecordingAsPicture();
}

bool OpaqueContext::containsOpaqueBounds(const Rect& bounds) const {
  return containContourBound(bounds);
}

bool OpaqueContext::canAppend(const Contour& contour, const Matrix& matrix, const ClipStack& clip,
                              const Brush& brush) {
  if (!clip.isSame(pendingClip) || matrix != pendingMatrix) {
    return false;
  }
  if (pendingBrushes.empty() || brush.maskFilter != pendingBrushes.back().maskFilter) {
    return false;
  }
  return contour == pendingContour;
}

Rect GetGlobalBounds(const Matrix& matrix, const ClipStack& clip, const Rect& localBounds) {
  auto globalBounds = matrix.mapRect(localBounds);
  if (clip.state() != ClipState::WideOpen) {
    if (!globalBounds.intersect(clip.bounds())) {
      return Rect::MakeEmpty();
    }
  }
  return globalBounds;
}

void OpaqueContext::flushPendingContour(const Contour& contour, const Matrix& matrix,
                                        const ClipStack& clip, const Brush& brush) {
  PendingOpaqueAutoReset autoReset(this, contour, matrix, clip, brush);
  if (pendingContour.type == Contour::Type::None) {
    return;
  }
  Rect localBounds = pendingContour.getBounds();
  if (pendingContour.hasStroke) {
    ApplyStrokeToBounds(pendingContour.stroke, &localBounds, pendingMatrix);
  }
  auto globalBounds = GetGlobalBounds(pendingMatrix, pendingClip, localBounds);
  bool fillIsFull = false;
  if (containContourBound(globalBounds) && !pendingContour.isInverseFillType()) {
    return;
  }
  for (auto& pendingBrush : pendingBrushes) {
    fillIsFull =
        fillIsFull || ((pendingBrush.shader == nullptr || !pendingBrush.shader->isAImage()) &&
                       !pendingBrush.maskFilter);
    pendingContour.draw(pictureContext, pendingMatrix, pendingClip, pendingBrush);
  }
  if (fillIsFull && pendingMatrix.rectStaysRect() && pendingContour.type < Contour::Type::Path &&
      !pendingContour.isInverseFillType() && !pendingContour.hasStroke) {
    // Shrink opaque bounds to fully covered pixels if clip has AA edges. We conservatively shrink
    // all edges rather than checking each clip element's AA status per edge, as the cost is minimal
    // (fewer optimization opportunities).
    auto hasAAClip =
        std::any_of(pendingClip.elements().begin(), pendingClip.elements().end(),
                    [](const ClipElement& e) { return e.isValid() && e.isAntiAlias(); });
    if (hasAAClip) {
      globalBounds.roundIn();
    }
    if (pendingContour.type == Contour::Type::Rect) {
      mergeContourBound(globalBounds);
    } else if (pendingContour.type == Contour::Type::RRect) {
      // Compute the largest axis-aligned rectangle inscribed in the RRect: inset each edge by
      // the max of its two adjacent corner radii so the result stays fully inside all corner arcs.
      const auto& radii = pendingContour.rRect.radii();
      const auto topY = std::max(radii[0].y, radii[1].y);
      const auto bottomY = std::max(radii[2].y, radii[3].y);
      const auto leftX = std::max(radii[0].x, radii[3].x);
      const auto rightX = std::max(radii[1].x, radii[2].x);
      localBounds.setLTRB(localBounds.left + leftX, localBounds.top + topY,
                          localBounds.right - rightX, localBounds.bottom - bottomY);
      if (localBounds.isSorted()) {
        globalBounds = GetGlobalBounds(pendingMatrix, pendingClip, localBounds);
        if (hasAAClip) {
          globalBounds.roundIn();
        }
        mergeContourBound(globalBounds);
      }
    }
  }
}

/**
 * Converts a brush to "opaque mode" by applying AlphaThreshold filter.
 * Semi-transparent pixels become fully opaque, while fully transparent pixels remain transparent.
 */
Brush GetOpaqueBrush(const Brush& brush) {
  Color color;
  if (brush.shader && !brush.shader->asColor(&color)) {
    auto opaqueBrush = brush;
    opaqueBrush.colorFilter = ColorFilter::AlphaThreshold(OPAQUE_THRESHOLD);
    return opaqueBrush;
  }
  // Src + coverage AA may cause edge artifacts, use SrcOver instead.
  auto opaqueBrush = Brush(Color::White(), BlendMode::SrcOver, false);
  opaqueBrush.maskFilter = brush.maskFilter;
  return opaqueBrush;
}

void OpaqueContext::appendFill(const Brush& brush) {
  auto& pendingShader = pendingBrushes.back().shader;
  if (pendingShader == nullptr) {
    return;
  }
  pendingBrushes.emplace_back(GetOpaqueBrush(brush));
}

void OpaqueContext::resetPendingContour(const Contour& contour, const Matrix& matrix,
                                        const ClipStack& clip, const Brush& brush) {
  pendingContour = contour;
  pendingMatrix = matrix;
  pendingClip = clip;
  pendingBrushes = {GetOpaqueBrush(brush)};
}

bool OpaqueContext::Contour::isInverseFillType() const {
  if (type < Type::Path) {
    return false;
  }
  if (type == Type::Path) {
    return path.isInverseFillType();
  }
  return shape->isInverseFillType();
}

Rect OpaqueContext::Contour::getBounds() const {
  switch (type) {
    case Type::Fill: {
      return Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    }
    case Type::Rect: {
      return rect;
    }
    case Type::RRect: {
      return rRect.rect();
    }
    case Type::Path: {
      return path.getBounds();
    }
    case Type::Shape: {
      return shape->getBounds();
    }
    default:
      break;
  }
  return Rect::MakeEmpty();
}

void OpaqueContext::Contour::draw(PictureContext& context, const Matrix& matrix,
                                  const ClipStack& clip, const Brush& brush) const {
  const Stroke* strokePtr = hasStroke ? &stroke : nullptr;
  switch (type) {
    case Type::Fill: {
      context.drawFill(brush);
      break;
    }
    case Type::Rect: {
      context.drawRect(rect, matrix, clip, brush, strokePtr);
      break;
    }
    case Type::RRect: {
      context.drawRRect(rRect, matrix, clip, brush, strokePtr);
      break;
    }
    case Type::Path: {
      context.drawPath(path, matrix, clip, brush);
      break;
    }
    case Type::Shape: {
      context.drawShape(shape, matrix, clip, brush, strokePtr);
      break;
    }
    default:
      break;
  }
}

bool OpaqueContext::Contour::operator==(const Contour& other) const {
  if (type != other.type || hasStroke != other.hasStroke) {
    return false;
  }
  if (hasStroke && stroke != other.stroke) {
    return false;
  }
  switch (type) {
    case Type::Rect:
      return rect == other.rect;
    case Type::RRect:
      return rRect.rect() == other.rRect.rect() && rRect.radii() == other.rRect.radii();
    case Type::Path:
      return path == other.path;
    case Type::Shape:
      return shape == other.shape;
    default:
      break;
  }
  return true;
}

OpaqueContext::Contour& OpaqueContext::Contour::operator=(const Contour& other) {
  if (this == &other) return *this;
  switch (type) {
    case Type::Path:
      path.~Path();
      break;
    case Type::Shape:
      shape.~shared_ptr<Shape>();
      break;
    default:
      break;
  }
  type = other.type;
  hasStroke = other.hasStroke;
  stroke = other.stroke;
  switch (type) {
    case Type::Rect:
      new (&rect) Rect(other.rect);
      break;
    case Type::RRect:
      new (&rRect) RRect(other.rRect);
      break;
    case Type::Path:
      new (&path) Path(other.path);
      break;
    case Type::Shape:
      new (&shape) std::shared_ptr<Shape>(other.shape);
      break;
    case Type::Fill:
    case Type::None:
      break;
  }
  return *this;
}

}  // namespace tgfx
