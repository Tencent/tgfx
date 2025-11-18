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
#include <algorithm>
#include "../../include/tgfx/core/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/ShapeUtils.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
static bool GetLocalPoint(const Matrix& matrix, float deviceX, float deviceY, Point* localPoint) {
  Matrix inverseMatrix;
  if (!matrix.invert(&inverseMatrix)) {
    return {};
  }
  *localPoint = inverseMatrix.mapXY(deviceX, deviceY);
  return true;
}

void HitTestContext::drawFill(const Fill&) {
  hit = true;
}

void HitTestContext::drawRect(const Rect& rect, const MCState& state, const Fill&,
                              const Stroke* stroke) {
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
  if (stroke == nullptr) {
    if (!rect.contains(local.x, local.y)) {
      return;
    }
  } else {
    if (shapeHitTest) {
      Path path = {};
      path.addRect(rect);
      stroke->applyToPath(&path);
      if (!path.contains(local.x, local.y)) {
        return;
      }
    } else {
      auto strokeRect = rect;
      ApplyStrokeToBounds(*stroke, &strokeRect);
      if (!strokeRect.contains(local.x, local.y)) {
        return;
      }
    }
  }
  if (checkClip(state.clip, local)) {
    hit = true;
  }
}

void HitTestContext::drawRRect(const RRect& rRect, const MCState& state, const Fill&,
                               const Stroke* stroke) {
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
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
  if (checkClip(state.clip, local)) {
    hit = true;
  }
}

void HitTestContext::drawPath(const Path& path, const MCState& state, const Fill&) {
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
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
  if (checkClip(state.clip, local)) {
    hit = true;
  }
}

void HitTestContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill&,
                               const Stroke* stroke) {
  DEBUG_ASSERT(shape != nullptr);
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
  if (shapeHitTest) {
    // Measure doesn't require high-precision paths, so ignore resolution scale here.
    auto path = shape->getPath();
    if (stroke) {
      stroke->applyToPath(&path);
    }
    if (!path.contains(local.x, local.y)) {
      return;
    }
  } else {
    auto bounds = shape->getBounds();
    if (stroke) {
      ApplyStrokeToBounds(*stroke, &bounds);
    }
    if (!bounds.contains(local.x, local.y)) {
      return;
    }
  }
  if (checkClip(state.clip, local)) {
    hit = true;
  }
}

void HitTestContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions&,
                               const MCState& state, const Fill&) {
  // Images are always checked against their bounding box.
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
  auto imageBounds = Rect::MakeWH(image->width(), image->height());
  if (imageBounds.contains(local.x, local.y) && checkClip(state.clip, local)) {
    hit = true;
  }
}

void HitTestContext::drawImageRect(std::shared_ptr<Image>, const Rect&, const Rect& dstRect,
                                   const SamplingOptions&, const MCState& state, const Fill&,
                                   SrcRectConstraint) {
  // Images are always checked against their bounding box.
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
  if (dstRect.contains(local.x, local.y) && checkClip(state.clip, local)) {
    hit = true;
  }
}

void HitTestContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                      const MCState& state, const Fill&, const Stroke* stroke) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  auto maxScale = state.matrix.getMaxScale();
  if (FloatNearlyZero(maxScale)) {
    return;
  }
  if (shapeHitTest && glyphRunList->hasOutlines()) {
    Path glyphPath = {};
    if (stroke) {
      glyphRunList->getPath(&glyphPath);
      stroke->applyToPath(&glyphPath);
      glyphPath.transform(state.matrix);
    } else {
      glyphRunList->getPath(&glyphPath, &state.matrix);
    }
    if (!glyphPath.contains(deviceX, deviceY)) {
      return;
    }
  } else {
    auto localBounds = glyphRunList->getBounds();
    if (stroke) {
      ApplyStrokeToBounds(*stroke, &localBounds);
    }
    auto deviceBounds = state.matrix.mapRect(localBounds);
    if (!deviceBounds.contains(deviceX, deviceY)) {
      return;
    }
  }
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
  if (checkClip(state.clip, local)) {
    hit = true;
  }
}

void HitTestContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> imageFilter, const MCState& state,
                               const Fill&) {
  DEBUG_ASSERT(picture != nullptr);
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
  if (imageFilter) {
    auto localBounds = shapeHitTest ? picture->getTightBounds(nullptr) : picture->getBounds();
    localBounds = imageFilter->filterBounds(localBounds);
    if (!localBounds.contains(local.x, local.y)) {
      return;
    }
  } else {
    if (!picture->hitTestPoint(local.x, local.y, shapeHitTest)) {
      return;
    }
  }
  if (checkClip(state.clip, local)) {
    hit = true;
  }
}

void HitTestContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  Point local = {};
  if (!GetLocalPoint(state.matrix, deviceX, deviceY, &local)) {
    return;
  }
  // Don't use picture->playback() here because it will not stop at the first hit.
  if (!picture->hitTestPoint(local.x, local.y, shapeHitTest)) {
    return;
  }
  if (checkClip(state.clip, local)) {
    hit = true;
  }
}

bool HitTestContext::checkClip(const Path& clip, const Point& local) const {
  if (!clip.isInverseFillType() && clip.isEmpty()) {
    return false;
  }
  if (shapeHitTest || clip.isInverseFillType()) {
    return clip.contains(local.x, local.y);
  }
  auto clipBounds = clip.getBounds();
  return clipBounds.contains(local.x, local.y);
}

}  // namespace tgfx
