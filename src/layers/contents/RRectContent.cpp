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

#include "RRectContent.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Path.h"

namespace tgfx {

RRectContent::RRectContent(const RRect& rRect, const LayerPaint& paint)
    : GeometryContent(paint), rRect(rRect) {
}

Rect RRectContent::onGetBounds() const {
  return rRect.rect;
}

Path RRectContent::getFilledPath() const {
  Path path = {};
  path.addRRect(rRect);
  if (stroke) {
    stroke->applyToPath(&path);
  }
  return path;
}

Rect RRectContent::getTightBounds(const Matrix& matrix) const {
  if (stroke) {
    auto strokedPath = getFilledPath();
    strokedPath.transform(matrix);
    return strokedPath.getBounds();
  }
  return matrix.mapRect(rRect.rect);
}

bool RRectContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  return getFilledPath().contains(localX, localY);
}

void RRectContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawRRect(rRect, paint);
}

bool RRectContent::onHasSameGeometry(const GeometryContent* other) const {
  auto otherRRect = static_cast<const RRectContent*>(other)->rRect;
  return rRect.rect == otherRRect.rect && rRect.radii == otherRRect.radii;
}

}  // namespace tgfx
