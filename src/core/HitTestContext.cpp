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

#include "HitTestContext.h"
#include "core/utils/ApplyStrokeToBounds.h"
#include "core/utils/MathExtra.h"
#include "utils/Log.h"

namespace tgfx {
void HitTestContext::drawFill(const Fill& fill) {
  if (!fill.nothingToDraw()) {
    hit = true;
  }
}

void HitTestContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  auto local = state.matrix.mapXY(testX, testY);
  if (rect.contains(local.x, local.y) && checkClipAndFill(state.clip, fill, local)) {
    hit = true;
  }
}

void HitTestContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                               const Stroke* stroke) {
  auto local = state.matrix.mapXY(testX, testY);
  if (shapeHitTest) {
    Path path = {};
    path.addRRect(rRect);
    if (stroke) {
      stroke->applyToPath(&path);
    }
    if (!path.contains(local.x, local.y)) {
      return;
    }
  } else {
    auto rect = rRect.rect;
    if (stroke) {
      ApplyStrokeToBounds(*stroke, &rect);
    }
    if (!rect.contains(local.x, local.y)) {
      return;
    }
  }
  if (checkClipAndFill(state.clip, fill, local)) {
    hit = true;
  }
}

void HitTestContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  auto local = state.matrix.mapXY(testX, testY);
  if (shapeHitTest) {
    if (!path.contains(local.x, local.y)) {
      return;
    }
  } else {
    auto bounds = path.getBounds();
    if (!bounds.contains(local.x, local.y)) {
      return;
    }
  }
  if (checkClipAndFill(state.clip, fill, local)) {
    hit = true;
  }
}

void HitTestContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                               const Fill& fill) {
  DEBUG_ASSERT(shape != nullptr);
  auto local = state.matrix.mapXY(testX, testY);
  if (shapeHitTest) {
    auto path = shape->getPath();
    if (!path.contains(local.x, local.y)) {
      return;
    }
  } else {
    auto bounds = shape->getBounds();
    if (!bounds.contains(local.x, local.y)) {
      return;
    }
  }
  if (checkClipAndFill(state.clip, fill, local)) {
    hit = true;
  }
}

void HitTestContext::drawImageRect(std::shared_ptr<Image>, const Rect&, const Rect& dstRect,
                                   const SamplingOptions&, const MCState& state, const Fill& fill,
                                   SrcRectConstraint) {
  // Images are always checked against their bounding box.
  auto local = state.matrix.mapXY(testX, testY);
  if (dstRect.contains(local.x, local.y) && checkClipAndFill(state.clip, fill, local)) {
    hit = true;
  }
}

void HitTestContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                      const MCState& state, const Fill& fill,
                                      const Stroke* stroke) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  auto maxScale = state.matrix.getMaxScale();
  if (FloatNearlyZero(maxScale)) {
    return;
  }
  auto local = state.matrix.mapXY(testX, testY);
  if (shapeHitTest && glyphRunList->hasOutlines()) {
    Path glyphPath = {};
    glyphRunList->getPath(&glyphPath, maxScale);
    if (stroke) {
      stroke->applyToPath(&glyphPath);
    }
    if (!glyphPath.contains(local.x, local.y)) {
      return;
    }
  } else {
    auto localBounds = glyphRunList->getBounds(maxScale);
    if (stroke) {
      ApplyStrokeToBounds(*stroke, &localBounds);
    }
    if (!localBounds.contains(local.x, local.y)) {
      return;
    }
  }
  if (checkClipAndFill(state.clip, fill, local)) {
    hit = true;
  }
}

void HitTestContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> imageFilter, const MCState& state,
                               const Fill& fill) {
  DEBUG_ASSERT(picture != nullptr);
  auto local = state.matrix.mapXY(testX, testY);
  if (imageFilter) {
    auto localBounds = picture->getBounds(nullptr, shapeHitTest);
    localBounds = imageFilter->filterBounds(localBounds);
    if (!localBounds.contains(local.x, local.y)) {
      return;
    }
  } else {
    if (!picture->hitTestPoint(local.x, local.y, shapeHitTest)) {
      return;
    }
  }
  if (checkClipAndFill(state.clip, fill, local)) {
    hit = true;
  }
}

void HitTestContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  auto local = state.matrix.mapXY(testX, testY);
  // Don't use picture->playback() here because it will not stop at the first hit.
  if (!picture->hitTestPoint(local.x, local.y, shapeHitTest)) {
    return;
  }
  if (checkClipAndFill(state.clip, {}, local)) {
    hit = true;
  }
}

bool HitTestContext::checkClipAndFill(const Path& clip, const Fill& fill,
                                      const Point& local) const {
  if (fill.nothingToDraw() || (!clip.isInverseFillType() && clip.isEmpty())) {
    return false;
  }
  if (shapeHitTest || clip.isInverseFillType()) {
    return clip.contains(local.x, local.y);
  }
  auto clipBounds = clip.getBounds();
  return clipBounds.contains(local.x, local.y);
}

}  // namespace tgfx
