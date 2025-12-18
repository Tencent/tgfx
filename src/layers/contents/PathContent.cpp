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

#include "PathContent.h"
#include "core/utils/StrokeUtils.h"

namespace tgfx {

PathContent::PathContent(Path path, const LayerPaint& paint)
    : GeometryContent(paint), path(std::move(path)) {
}

Rect PathContent::onGetBounds() const {
  return path.getBounds();
}

Rect PathContent::getBounds() const {
  auto bounds = onGetBounds();
  if (stroke) {
    // Path may contain sharp corners, so we need to apply miter limit to the bounds.
    ApplyStrokeToBounds(*stroke, &bounds, true);
  }
  return bounds;
}

Path PathContent::getFilledPath() const {
  Path result = path;
  if (stroke) {
    stroke->applyToPath(&result);
  }
  return result;
}

Rect PathContent::getTightBounds(const Matrix& matrix) const {
  auto strokedPath = getFilledPath();
  strokedPath.transform(matrix);
  return strokedPath.getBounds();
}

bool PathContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  return getFilledPath().contains(localX, localY);
}

void PathContent::onDraw(Canvas* canvas, const Paint& paint) const {
  canvas->drawPath(path, paint);
}

bool PathContent::onHasSameGeometry(const GeometryContent* other) const {
  return path == static_cast<const PathContent*>(other)->path;
}

std::optional<Path> PathContent::onAsClipPath() const {
  return path;
}

}  // namespace tgfx
