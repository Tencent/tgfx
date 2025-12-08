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

#include "ForegroundContent.h"

namespace tgfx {
Rect ForegroundContent::getBounds() const {
  auto bounds = foreground->getBounds();
  if (background) {
    bounds.join(background->getBounds());
  }
  return bounds;
}

Rect ForegroundContent::getTightBounds(const Matrix& matrix) const {
  auto bounds = foreground->getTightBounds(&matrix);
  if (background) {
    bounds.join(background->getTightBounds(&matrix));
  }
  return bounds;
}

bool ForegroundContent::hitTestPoint(float localX, float localY, bool shapeHitTest) const {
  if (foreground->hitTestPoint(localX, localY, shapeHitTest)) {
    return true;
  }
  if (background && background->hitTestPoint(localX, localY, shapeHitTest)) {
    return true;
  }
  return false;
}

void ForegroundContent::drawDefault(Canvas* canvas, const BrushModifier* modifier) const {
  if (background) {
    background->playback(canvas, modifier);
  }
}

void ForegroundContent::drawForeground(Canvas* canvas, const BrushModifier* modifier) const {
  foreground->playback(canvas, modifier);
}

void ForegroundContent::drawContour(Canvas* canvas, const BrushModifier* modifier) const {
  if (background) {
    background->playback(canvas, modifier);
  }
  foreground->playback(canvas, modifier);
}
}  // namespace tgfx