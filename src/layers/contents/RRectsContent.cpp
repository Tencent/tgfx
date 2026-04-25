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

#include "RRectsContent.h"
#include "tgfx/core/Path.h"

namespace tgfx {

RRectsContent::RRectsContent(std::vector<RRect> rRects, const LayerPaint& paint)
    : DrawContent(paint), rRects(std::move(rRects)) {
}

Rect RRectsContent::getTightBounds(const Matrix& matrix) const {
  if (stroke) {
    auto strokedPath = getFilledPath();
    strokedPath.transform(matrix);
    return strokedPath.getBounds();
  }
  auto bounds = matrix.mapRect(rRects[0].rect);
  for (size_t i = 1; i < rRects.size(); ++i) {
    bounds.join(matrix.mapRect(rRects[i].rect));
  }
  return bounds;
}

bool RRectsContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  Path path = {};
  for (const auto& rRect : rRects) {
    path.addRRect(rRect);
    if (stroke) {
      stroke->applyToPath(&path);
    }
    if (path.contains(localX, localY)) {
      return true;
    }
    path.reset();
  }
  return false;
}

Rect RRectsContent::getCoverRect() const {
  if (stroke) {
    return Rect::MakeEmpty();
  }
  auto largest = Rect::MakeEmpty();
  auto largestArea = 0.0f;
  for (const auto& rr : rRects) {
    auto insetRect = rr.rect;
    insetRect.inset(rr.radii.x, rr.radii.y);
    if (insetRect.isEmpty()) {
      continue;
    }
    auto area = insetRect.width() * insetRect.height();
    if (area > largestArea) {
      largestArea = area;
      largest = insetRect;
    }
  }
  return largest;
}

Rect RRectsContent::onGetBounds() const {
  auto bounds = rRects[0].rect;
  for (size_t i = 1; i < rRects.size(); ++i) {
    bounds.join(rRects[i].rect);
  }
  return bounds;
}

void RRectsContent::onDraw(Canvas* canvas, const Paint& paint) const {
  for (const auto& rRect : rRects) {
    canvas->drawRRect(rRect, paint);
  }
}

bool RRectsContent::onHasSameGeometry(const GeometryContent* other) const {
  const auto& otherRRects = static_cast<const RRectsContent*>(other)->rRects;
  if (rRects.size() != otherRRects.size()) {
    return false;
  }
  for (size_t i = 0; i < rRects.size(); ++i) {
    if (rRects[i].rect != otherRRects[i].rect || rRects[i].radii != otherRRects[i].radii) {
      return false;
    }
  }
  return true;
}

Path RRectsContent::getFilledPath() const {
  Path path = {};
  for (const auto& rRect : rRects) {
    path.addRRect(rRect);
  }
  if (stroke) {
    stroke->applyToPath(&path);
  }
  return path;
}

}  // namespace tgfx
