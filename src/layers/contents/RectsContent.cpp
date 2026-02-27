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

#include "RectsContent.h"
#include "tgfx/core/Path.h"

namespace tgfx {

RectsContent::RectsContent(std::vector<Rect> rects, const LayerPaint& paint)
    : DrawContent(paint), rects(std::move(rects)) {
}

Rect RectsContent::getTightBounds(const Matrix& matrix) const {
  if (stroke) {
    auto strokedPath = getFilledPath();
    strokedPath.transform(matrix);
    return strokedPath.getBounds();
  }
  auto bounds = matrix.mapRect(rects[0]);
  for (size_t i = 1; i < rects.size(); ++i) {
    bounds.join(matrix.mapRect(rects[i]));
  }
  return bounds;
}

bool RectsContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  if (stroke) {
    Path path = {};
    for (const auto& rect : rects) {
      path.addRect(rect);
      stroke->applyToPath(&path);
      if (path.contains(localX, localY)) {
        return true;
      }
      path.reset();
    }
    return false;
  }
  for (const auto& rect : rects) {
    if (rect.contains(localX, localY)) {
      return true;
    }
  }
  return false;
}

Rect RectsContent::onGetBounds() const {
  auto bounds = rects[0];
  for (size_t i = 1; i < rects.size(); ++i) {
    bounds.join(rects[i]);
  }
  return bounds;
}

void RectsContent::onDraw(Canvas* canvas, const Paint& paint) const {
  for (const auto& rect : rects) {
    canvas->drawRect(rect, paint);
  }
}

bool RectsContent::onHasSameGeometry(const GeometryContent* other) const {
  return rects == static_cast<const RectsContent*>(other)->rects;
}

Path RectsContent::getFilledPath() const {
  Path path = {};
  for (const auto& rect : rects) {
    path.addRect(rect);
  }
  if (stroke) {
    stroke->applyToPath(&path);
  }
  return path;
}

}  // namespace tgfx
