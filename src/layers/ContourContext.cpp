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

#include "ContourContext.h"
#include "core/utils/RectToRectMatrix.h"
#include "core/utils/StrokeUtils.h"
#include "layers/OpaqueThreshold.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {

class PendingContourAutoReset {
 public:
  PendingContourAutoReset(ContourContext* context, const ContourContext::Contour& contour,
                          MCState state, Brush brush)
      : context(context), contour(contour), state(std::move(state)), brush(std::move(brush)) {
  }

  ~PendingContourAutoReset() {
    context->resetPendingContour(contour, state, brush);
  }

 private:
  ContourContext* context = nullptr;
  ContourContext::Contour contour;
  MCState state;
  Brush brush;
};

ContourContext::ContourContext() {
  contourBounds.reserve(3);
}

ContourContext::~ContourContext() {
  delete canvas;
}

Canvas* ContourContext::beginRecording() {
  if (canvas == nullptr) {
    canvas = new Canvas(this);
  } else {
    canvas->resetStateStack();
  }
  pendingContour = {};
  pendingState = {};
  pendingBrushes.clear();
  contourBounds.clear();
  pictureContext.clear();
  return canvas;
}

void ContourContext::drawFill(const Brush& brush) {
  static Contour FillContour = Contour(Contour::Type::Fill);
  drawContour(FillContour, {}, brush);
}

void ContourContext::drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                              const Stroke* stroke) {
  drawContour(Contour(rect, stroke), state, brush);
}

void ContourContext::drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                               const Stroke* stroke) {
  drawContour(Contour(rRect, stroke), state, brush);
}

void ContourContext::drawPath(const Path& path, const MCState& state, const Brush& brush) {
  drawContour(Contour(path), state, brush);
}

void ContourContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                               const Brush& brush, const Stroke* stroke) {
  drawContour(Contour(std::move(shape), stroke), state, brush);
}

void ContourContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                               const MCState& state, const Brush& brush) {
  auto newBrush = brush;
  newBrush.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
  drawRect(Rect::MakeWH(image->width(), image->height()), state, newBrush, nullptr);
}

void ContourContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                   const Rect& dstRect, const SamplingOptions& sampling,
                                   const MCState& state, const Brush& brush,
                                   SrcRectConstraint constraint) {
  if (constraint != SrcRectConstraint::Strict) {
    auto newState = state;
    newState.matrix.preConcat(MakeRectToRectMatrix(srcRect, dstRect));
    auto path = Path();
    path.addRect(dstRect);
    path.transform(state.matrix);
    newState.clip.addPath(path);
    auto newBrush = brush;
    newBrush.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
    drawRect(dstRect, newState, newBrush, nullptr);
    return;
  }
  auto bounds = state.matrix.mapRect(dstRect);
  if (containContourBound(bounds)) {
    return;
  }
  pictureContext.drawImageRect(image, srcRect, dstRect, sampling, state, brush, constraint);
}

void ContourContext::drawTextBlob(std::shared_ptr<TextBlob> textBlob, const MCState& state,
                                  const Brush& brush, const Stroke* stroke) {
  auto bounds = textBlob->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds, state.matrix);
  }
  bounds = state.matrix.mapRect(bounds);
  if (containContourBound(bounds)) {
    return;
  }
  pictureContext.drawTextBlob(textBlob, state, brush, stroke);
}

void ContourContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  picture->playback(this, state);
}

void ContourContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> filter, const MCState& state,
                               const Brush& brush) {
  if (brush.nothingToDraw()) {
    return;
  }
  if (!filter && !brush.maskFilter) {
    drawPicture(picture, state);
    return;
  }
  if (!picture->hasUnboundedFill()) {
    auto bounds = picture->getBounds();
    if (filter) {
      bounds = filter->filterBounds(bounds);
    }
    bounds = state.matrix.mapRect(bounds);
    if (containContourBound(bounds)) {
      return;
    }
  }
  flushPendingContour();
  pictureContext.drawLayer(picture, filter, state, brush);
}

void ContourContext::drawContour(const Contour& contour, const MCState& state, const Brush& brush) {
  if (canAppend(contour, state, brush)) {
    appendFill(brush);
    return;
  }
  flushPendingContour(contour, state, brush);
}

bool ContourContext::containContourBound(const Rect& bounds) {
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

void ContourContext::mergeContourBound(const Rect& bounds) {
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

std::shared_ptr<Picture> ContourContext::finishRecordingAsPicture() {
  flushPendingContour();
  return pictureContext.finishRecordingAsPicture();
}

bool ContourContext::canAppend(const Contour& contour, const MCState& state, const Brush& brush) {
  if (state.clip != pendingState.clip || state.matrix != pendingState.matrix) {
    return false;
  }
  if (pendingBrushes.empty() || brush.maskFilter != pendingBrushes.back().maskFilter) {
    return false;
  }
  return contour == pendingContour;
}

Rect GetGlobalBounds(const MCState& state, const Rect& localBounds) {
  auto globalBounds = state.matrix.mapRect(localBounds);
  if (!state.clip.isInverseFillType()) {
    if (!globalBounds.intersect(state.clip.getBounds())) {
      return Rect::MakeEmpty();
    }
  }
  return globalBounds;
}

void ContourContext::flushPendingContour(const Contour& contour, const MCState& state,
                                         const Brush& brush) {
  PendingContourAutoReset autoReset(this, contour, state, brush);
  if (pendingContour.type == Contour::Type::None) {
    return;
  }
  Rect localBounds = pendingContour.getBounds();
  if (pendingContour.hasStroke) {
    ApplyStrokeToBounds(pendingContour.stroke, &localBounds, pendingState.matrix);
  }
  auto globalBounds = GetGlobalBounds(pendingState, localBounds);
  bool fillIsFull = false;
  if (containContourBound(globalBounds) && !pendingContour.isInverseFillType()) {
    return;
  }
  for (auto& pendingBrush : pendingBrushes) {
    fillIsFull =
        fillIsFull || ((pendingBrush.shader == nullptr || !pendingBrush.shader->isAImage()) &&
                       !pendingBrush.maskFilter);
    pendingContour.draw(pictureContext, pendingState, pendingBrush);
  }
  if (fillIsFull && pendingState.matrix.rectStaysRect() &&
      pendingContour.type < Contour::Type::Path && !pendingContour.isInverseFillType() &&
      !pendingContour.hasStroke) {
    if (pendingContour.type == Contour::Type::Rect) {
      mergeContourBound(globalBounds);
    } else if (pendingContour.type == Contour::Type::RRect) {
      localBounds.inset(pendingContour.rRect.radii.x, pendingContour.rRect.radii.y);
      if (localBounds.isSorted()) {
        globalBounds = GetGlobalBounds(pendingState, localBounds);
        mergeContourBound(globalBounds);
      }
    }
  }
}

Brush GetContourBrush(const Brush& brush) {
  Color color;
  if (brush.shader && !brush.shader->asColor(&color)) {
    auto contourBrush = brush;
    contourBrush.colorFilter = ColorFilter::AlphaThreshold(OPAQUE_THRESHOLD);
    return contourBrush;
  }
  // Src + coverage AA may cause edge artifacts, use SrcOver instead.
  auto contourBrush = Brush(Color::White(), BlendMode::SrcOver, brush.antiAlias);
  contourBrush.maskFilter = brush.maskFilter;
  return contourBrush;
}

void ContourContext::appendFill(const Brush& brush) {
  auto& pendingShader = pendingBrushes.back().shader;
  if (pendingShader == nullptr) {
    return;
  }
  pendingBrushes.emplace_back(GetContourBrush(brush));
}

void ContourContext::resetPendingContour(const Contour& contour, const MCState& state,
                                         const Brush& brush) {
  pendingContour = contour;
  pendingState = state;
  pendingBrushes = {GetContourBrush(brush)};
}

bool ContourContext::Contour::isInverseFillType() const {
  if (type < Type::Path) {
    return false;
  }
  if (type == Type::Path) {
    return path.isInverseFillType();
  }
  return shape->isInverseFillType();
}

Rect ContourContext::Contour::getBounds() const {
  switch (type) {
    case Type::Fill: {
      return Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    }
    case Type::Rect: {
      return rect;
    }
    case Type::RRect: {
      return rRect.rect;
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

void ContourContext::Contour::draw(PictureContext& context, const MCState& state,
                                   const Brush& brush) const {
  const Stroke* strokePtr = hasStroke ? &stroke : nullptr;
  switch (type) {
    case Type::Fill: {
      context.drawFill(brush);
      break;
    }
    case Type::Rect: {
      context.drawRect(rect, state, brush, strokePtr);
      break;
    }
    case Type::RRect: {
      context.drawRRect(rRect, state, brush, strokePtr);
      break;
    }
    case Type::Path: {
      context.drawPath(path, state, brush);
      break;
    }
    case Type::Shape: {
      context.drawShape(shape, state, brush, strokePtr);
      break;
    }
    default:
      break;
  }
}

bool ContourContext::Contour::operator==(const Contour& other) const {
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
      return rRect.rect == other.rRect.rect && rRect.radii == other.rRect.radii;
    case Type::Path:
      return path == other.path;
    case Type::Shape:
      return shape == other.shape;
    default:
      break;
  }
  return true;
}

ContourContext::Contour& ContourContext::Contour::operator=(const Contour& other) {
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
