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

namespace tgfx {

class PendingContourAutoReset {
 public:
  PendingContourAutoReset(ContourContext* context, const ContourContext::Contour& contour,
                          MCState state, Fill fill, const Stroke* stroke)
      : context(context), contour(contour), state(std::move(state)), fill(std::move(fill)),
        stroke(stroke) {
  }

  ~PendingContourAutoReset() {
    context->resetPendingContour(contour, state, fill, stroke);
  }

 private:
  ContourContext* context = nullptr;
  ContourContext::Contour contour;
  MCState state;
  Fill fill;
  const Stroke* stroke = nullptr;
};

ContourContext::ContourContext() {
  contourBounds.reserve(3);
}

void ContourContext::drawFill(const Fill& fill) {
  static Contour FillContour = Contour(Contour::Type::Fill);
  drawContour(FillContour, {}, fill);
}

void ContourContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill,
                              const Stroke* stroke) {
  drawContour(Contour(rect), state, fill, stroke);
}

void ContourContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                               const Stroke* stroke) {
  drawContour(Contour(rRect), state, fill, stroke);
}

void ContourContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  drawContour(Contour(path), state, fill);
}

void ContourContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill,
                               const Stroke* stroke) {
  drawContour(Contour(std::move(shape)), state, fill, stroke);
}

void ContourContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                               const MCState& state, const Fill& fill) {
  auto newFill = fill;
  newFill.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
  drawRect(Rect::MakeWH(image->width(), image->height()), state, newFill, nullptr);
}

void ContourContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                   const Rect& dstRect, const SamplingOptions& sampling,
                                   const MCState& state, const Fill& fill,
                                   SrcRectConstraint constraint) {
  if (constraint != SrcRectConstraint::Strict) {
    auto newState = state;
    newState.matrix.preConcat(MakeRectToRectMatrix(srcRect, dstRect));
    auto path = Path();
    path.addRect(dstRect);
    path.transform(state.matrix);
    newState.clip.addPath(path);
    auto newFill = fill;
    newFill.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
    drawRect(dstRect, newState, newFill, nullptr);
    return;
  }
  auto bounds = state.matrix.mapRect(dstRect);
  if (containContourBound(bounds)) {
    return;
  }
  pictureContext.drawImageRect(image, srcRect, dstRect, sampling, state, fill, constraint);
}

void ContourContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                      const MCState& state, const Fill& fill,
                                      const Stroke* stroke) {
  auto bounds = glyphRunList->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds);
  }
  bounds = state.matrix.mapRect(bounds);
  if (containContourBound(bounds)) {
    return;
  }
  pictureContext.drawGlyphRunList(glyphRunList, state, fill, stroke);
}

void ContourContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  picture->playback(this, state);
}

void ContourContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> filter, const MCState& state,
                               const Fill& fill) {
  if (fill.nothingToDraw()) {
    return;
  }
  if (!filter && !fill.maskFilter) {
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
  pictureContext.drawLayer(picture, filter, state, fill);
}

void ContourContext::drawContour(const Contour& contour, const MCState& state, const Fill& fill,
                                 const Stroke* stroke) {
  if (canAppend(contour, state, fill, stroke)) {
    appendFill(fill, stroke);
    return;
  }
  flushPendingContour(contour, state, fill, stroke);
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

bool ContourContext::canAppend(const Contour& contour, const MCState& state, const Fill& fill,
                               const Stroke* stroke) {
  if (state.clip != pendingState.clip || state.matrix != pendingState.matrix) {
    return false;
  }
  if (pendingFills.empty() || fill.maskFilter != pendingFills.back().maskFilter) {
    return false;
  }
  if (pendingStrokes.empty() != (stroke == nullptr)) {
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
                                         const Fill& fill, const Stroke* stroke) {
  PendingContourAutoReset autoReset(this, contour, state, fill, stroke);
  if (pendingContour.type == Contour::Type::None) {
    return;
  }
  Rect outset = Rect::MakeEmpty();
  for (auto pendingStroke : pendingStrokes) {
    auto rect = Rect::MakeEmpty();
    ApplyStrokeToBounds(*pendingStroke, &rect);
    if (rect.right > outset.right) {
      outset = rect;
    }
  }
  Rect localBounds = pendingContour.getBounds();
  localBounds.outset(outset.right, outset.top);
  auto globalBounds = GetGlobalBounds(pendingState, localBounds);
  bool fillIsFull = false;
  if (containContourBound(globalBounds) && !pendingContour.isInverseFillType()) {
    return;
  }
  for (size_t i = 0; i < pendingFills.size(); i++) {
    auto& pendingFill = pendingFills[i];
    const Stroke* pendingStroke = pendingStrokes.size() > i ? pendingStrokes[i] : nullptr;
    fillIsFull =
        fillIsFull || ((pendingFill.shader == nullptr || !pendingFill.shader->isAImage()) &&
                       !pendingFill.maskFilter);
    pendingContour.draw(pictureContext, pendingState, pendingFill, pendingStroke);
  }
  if (fillIsFull && pendingState.matrix.rectStaysRect() &&
      pendingContour.type < Contour::Type::Path && !pendingContour.isInverseFillType()) {
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

Fill GetContourFill(const Fill& fill) {
  Color color;
  if (fill.shader && !fill.shader->asColor(&color)) {
    auto contourFill = fill;
    contourFill.colorFilter = ColorFilter::AlphaThreshold(OPAQUE_THRESHOLD);
    return contourFill;
  }
  // Src + coverage AA may cause edge artifacts, use SrcOver instead.
  auto contourFill = Fill(Color::White(), BlendMode::SrcOver, fill.antiAlias);
  contourFill.maskFilter = fill.maskFilter;
  return contourFill;
}

void ContourContext::appendFill(const Fill& fill, const Stroke* stroke) {
  auto& pendingShader = pendingFills.back().shader;
  if (pendingShader == nullptr) {
    return;
  }
  pendingFills.emplace_back(GetContourFill(fill));
  if (stroke) {
    pendingStrokes.emplace_back(stroke);
  }
}

void ContourContext::resetPendingContour(const Contour& contour, const MCState& state,
                                         const Fill& fill, const Stroke* stroke) {
  pendingContour = contour;
  pendingState = state;
  pendingFills = {GetContourFill(fill)};
  if (stroke) {
    pendingStrokes = {stroke};
  } else {
    pendingStrokes.clear();
  }
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

void ContourContext::Contour::draw(PictureContext& context, const MCState& state, const Fill& fill,
                                   const Stroke* stroke) const {
  switch (type) {
    case Type::Fill: {
      context.drawFill(fill);
      break;
    }
    case Type::Rect: {
      context.drawRect(rect, state, fill, stroke);
      break;
    }
    case Type::RRect: {
      context.drawRRect(rRect, state, fill, stroke);
      break;
    }
    case Type::Path: {
      context.drawPath(path, state, fill);
      break;
    }
    case Type::Shape: {
      context.drawShape(shape, state, fill, stroke);
      break;
    }
    default:
      break;
  }
}

bool ContourContext::Contour::operator==(const Contour& other) const {
  if (type != other.type) {
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
