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
#include "layers/OpaqueThreshold.h"
#include "utils/ApplyStrokeToBounds.h"
#include "utils/RectToRectMatrix.h"

namespace tgfx {
ContourContext::ContourContext() {
  contourBounds.reserve(3);
}

void ContourContext::drawFill(const Fill& fill) {
  drawRect(Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX), {}, fill);
}

void ContourContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  Path path = {};
  path.addRect(rect);
  drawPath(path, state, fill);
}

void ContourContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                               const Stroke* stroke) {
  Path path = {};
  path.addRRect(rRect);
  auto shape = Shape::MakeFrom(path);
  if (canAppend(shape, state, fill) && pendingStrokes.empty() == (stroke == nullptr)) {
    appendFill(fill, stroke);
    return;
  }
  flushPendingShape(shape, state, fill, stroke);
}

void ContourContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  auto shape = Shape::MakeFrom(path);
  drawShape(shape, state, fill);
}

void ContourContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                               const Fill& fill) {
  if (canAppend(shape, state, fill)) {
    appendFill(fill);
    return;
  }
  flushPendingShape(shape, state, fill);
}

void ContourContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                               const MCState& state, const Fill& fill) {
  auto newFill = fill;
  newFill.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
  drawRect(Rect::MakeWH(image->width(), image->height()), state, newFill);
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
    drawRect(dstRect, newState, newFill);
    return;
  }
  auto bounds = state.matrix.mapRect(dstRect);
  if (containContourBound(bounds)) {
    return;
  }
  recordingContext.drawImageRect(image, srcRect, dstRect, sampling, state, fill, constraint);
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
  recordingContext.drawGlyphRunList(glyphRunList, state, fill, stroke);
}

void ContourContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  picture->playback(this, state);
}

void ContourContext::drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter>,
                               const MCState& state, const Fill&) {
  // skip image filter for contour
  picture->playback(this, state);
}

bool ContourContext::containContourBound(const Rect& bounds) {
  return std::any_of(contourBounds.begin(), contourBounds.end(),
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

void ContourContext::appendContourBound(const Rect& bounds) {
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
  flushPendingShape();
  return recordingContext.finishRecordingAsPicture();
}

bool ContourContext::canAppend(std::shared_ptr<Shape> shape, const MCState& state,
                               const Fill& fill) {
  if (state.clip != pendingState.clip || state.matrix != pendingState.matrix) {
    return false;
  }
  if (pendingFills.empty() || fill.maskFilter != pendingFills.back().maskFilter) {
    return false;
  }
  if (pendingShape->isSimplePath() && shape->isSimplePath()) {
    auto pendingPath = pendingShape->getPath();
    auto currentPath = shape->getPath();
    return pendingPath == currentPath;
  }
  return pendingShape == shape;
}

void ContourContext::flushPendingShape(std::shared_ptr<Shape> shape, const MCState& state,
                                       const Fill& fill, const Stroke* stroke) {
  if (pendingShape) {
    Rect outset = Rect::MakeEmpty();
    for (auto pendingStroke : pendingStrokes) {
      auto rect = Rect::MakeEmpty();
      ApplyStrokeToBounds(*pendingStroke, &rect);
      if (rect.right > outset.right) {
        outset = rect;
      }
    }
    Rect localBounds = pendingShape->getBounds();
    localBounds.outset(outset.right, outset.top);
    auto globalBounds = state.matrix.mapRect(localBounds);

    if (!containContourBound(globalBounds)) {
      for (size_t i = 0; i < pendingFills.size(); i++) {
        auto& pendingFill = pendingFills[i];
        const Stroke* pendingStroke = pendingStrokes.size() > i ? pendingStrokes[i] : nullptr;
        drawShapeInternal(pendingShape, pendingState, pendingFill, pendingStroke);
      }
      if (pendingState.matrix.isScaleTranslate() && pendingShape->isSimplePath() &&
          (fill.shader == nullptr || !fill.shader->isAImage()) && !fill.maskFilter) {
        RRect rRect = {};
        if (pendingShape->getPath().isRect()) {
          appendContourBound(globalBounds);
        } else if (pendingShape->getPath().isRRect(&rRect)) {
          localBounds.inset(rRect.radii.x, rRect.radii.y);
          if (localBounds.isSorted()) {
            globalBounds = state.matrix.mapRect(localBounds);
            appendContourBound(globalBounds);
          }
        }
      }
    }
  }
  pendingShape = std::move(shape);
  pendingState = state;
  pendingFills = {fill};
  pendingStrokes = stroke == nullptr ? std::vector<const Stroke*>() : std::vector{stroke};
}

void ContourContext::appendFill(const Fill& fill, const Stroke* stroke) {
  auto& pendingShader = pendingFills.back().shader;
  if (pendingShader == nullptr || !pendingShader->isAImage()) {
    return;
  }
  if (fill.shader != nullptr && fill.shader->isAImage()) {
    auto contourFill = fill;
    contourFill.colorFilter = ColorFilter::AlphaThreshold(OPAQUE_THRESHOLD);
    pendingFills.emplace_back(fill);
  } else {
    auto contourFill = Fill(Color::White(), BlendMode::Src, fill.antiAlias);
    contourFill.maskFilter = fill.maskFilter;
    pendingFills.emplace_back(contourFill);
  }
  if (stroke) {
    pendingStrokes.emplace_back(stroke);
  }
}

void ContourContext::drawShapeInternal(std::shared_ptr<Shape> shape, const MCState& state,
                                       const Fill& fill, const Stroke* stroke) {
  if (!shape->isSimplePath()) {
    recordingContext.drawShape(std::move(shape), state, fill);
    return;
  }
  auto path = shape->getPath();
  Rect rect = {};
  if (path.isRect(&rect)) {
    if (rect.left == -FLT_MAX && rect.top == -FLT_MAX && rect.right == FLT_MAX &&
        rect.bottom == FLT_MAX) {
      recordingContext.drawFill(fill);
    } else {
      recordingContext.drawRect(rect, state, fill);
    }
    return;
  }
  RRect rRect = {};
  if (path.isRRect(&rRect)) {
    recordingContext.drawRRect(rRect, state, fill, stroke);
  }
  recordingContext.drawPath(path, state, fill);
}

}  // namespace tgfx
