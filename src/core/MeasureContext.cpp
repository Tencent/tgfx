/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "MeasureContext.h"
#include "../../include/tgfx/core/Log.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Shape.h"
#include "utils/MathExtra.h"

namespace tgfx {
void MeasureContext::drawFill(const Fill&) {
}

void MeasureContext::drawRect(const Rect& rect, const MCState& state, const Fill&,
                              const Stroke* stroke) {
  if (stroke == nullptr) {
    addLocalBounds(state, rect);
    return;
  }
  auto localBounds = rect;
  ApplyStrokeToBounds(*stroke, &localBounds);
  addLocalBounds(state, localBounds);
}

void MeasureContext::drawRRect(const RRect& rRect, const MCState& state, const Fill&,
                               const Stroke* stroke) {
  auto rect = rRect.rect;
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &rect);
  }
  addLocalBounds(state, rect, false);
}

void MeasureContext::drawPath(const Path& path, const MCState& state, const Fill&) {
  if (computeTightBounds) {
    addTightBounds(path, state);
    return;
  }
  auto localBounds = path.getBounds();
  addLocalBounds(state, localBounds, path.isInverseFillType());
}

void MeasureContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill&,
                               const Stroke* stroke) {
  DEBUG_ASSERT(shape != nullptr);
  if (computeTightBounds) {
    auto path = shape->getPath();
    if (stroke) {
      // Measure doesn't require high-precision paths, so ignore resolution scale here.
      stroke->applyToPath(&path);
    }
    addTightBounds(path, state);
    return;
  }
  auto localBounds = shape->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &localBounds, true);
  }
  addLocalBounds(state, localBounds, shape->isInverseFillType());
}

void MeasureContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions&,
                               const MCState& state, const Fill&) {
  addLocalBounds(state, Rect::MakeWH(image->width(), image->height()));
}

void MeasureContext::drawImageRect(std::shared_ptr<Image>, const Rect&, const Rect& dstRect,
                                   const SamplingOptions&, const MCState& state, const Fill&,
                                   SrcRectConstraint) {
  addLocalBounds(state, dstRect);
}

void MeasureContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                      const MCState& state, const Fill&, const Stroke* stroke) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  auto localBounds =
      computeTightBounds ? glyphRunList->getTightBounds() : glyphRunList->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &localBounds);
  }
  addLocalBounds(state, localBounds);
}

void MeasureContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> imageFilter, const MCState& state,
                               const Fill&) {
  DEBUG_ASSERT(picture != nullptr);
  if (imageFilter) {
    auto localBounds = computeTightBounds ? picture->getTightBounds(nullptr) : picture->getBounds();
    localBounds = imageFilter->filterBounds(localBounds);
    addLocalBounds(state, localBounds);
  } else {
    Rect deviceBounds = {};
    if (computeTightBounds) {
      deviceBounds = picture->getTightBounds(&state.matrix);
    } else {
      deviceBounds = picture->getBounds();
      state.matrix.mapRect(&deviceBounds);
    }
    addDeviceBounds(state.clip, deviceBounds, picture->hasUnboundedFill());
  }
}

void MeasureContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, state);
}

void MeasureContext::addTightBounds(const Path& path, const MCState& state) {
  auto tempPath = path;
  tempPath.transform(state.matrix);
  auto deviceBounds = tempPath.getBounds();
  addDeviceBounds(state.clip, deviceBounds, path.isInverseFillType());
}

void MeasureContext::addLocalBounds(const MCState& state, const Rect& localBounds, bool unbounded) {
  auto deviceBounds = state.matrix.mapRect(localBounds);
  addDeviceBounds(state.clip, deviceBounds, unbounded);
}

void MeasureContext::addDeviceBounds(const Path& clip, const Rect& deviceBounds, bool unbounded) {
  if (clip.isInverseFillType()) {
    bounds.join(deviceBounds);
    return;
  }
  if (clip.isEmpty()) {
    return;
  }
  auto intersectBounds = clip.getBounds();
  if (!unbounded && !intersectBounds.intersect(deviceBounds)) {
    return;
  }
  bounds.join(intersectBounds);
}
}  // namespace tgfx
