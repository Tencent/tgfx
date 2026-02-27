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

#include "RectContent.h"

namespace tgfx {

RectContent::RectContent(const Rect& rect, const LayerPaint& paint)
    : DrawContent(paint), rect(rect) {
}

Rect RectContent::getTightBounds(const Matrix& matrix) const {
  if (stroke) {
    auto strokedPath = getFilledPath();
    strokedPath.transform(matrix);
    return strokedPath.getBounds();
  }
  return matrix.mapRect(rect);
}

bool RectContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  if (stroke) {
    return getFilledPath().contains(localX, localY);
  }
  return rect.contains(localX, localY);
}

Rect RectContent::onGetBounds() const {
  return rect;
}

void RectContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawRect(rect, paint);
}

bool RectContent::onHasSameGeometry(const GeometryContent* other) const {
  return rect == static_cast<const RectContent*>(other)->rect;
}

Path RectContent::getFilledPath() const {
  Path path = {};
  path.addRect(rect);
  if (stroke) {
    stroke->applyToPath(&path);
  }
  return path;
}

}  // namespace tgfx
