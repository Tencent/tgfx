/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/utils/Log.h"

namespace tgfx {
void MeasureContext::drawFill(const Fill&) {
}

void MeasureContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  addLocalBounds(state, fill, rect);
}

void MeasureContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                               const Stroke* stroke) {
  auto rect = rRect.rect;
  if (stroke) {
    stroke->applyToBounds(&rect);
  }
  addLocalBounds(state, fill, rect, false);
}

void MeasureContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  auto localBounds = path.getBounds();
  addLocalBounds(state, fill, localBounds, path.isInverseFillType());
}

void MeasureContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                               const Fill& fill) {
  auto localBounds = shape->getBounds();
  addLocalBounds(state, fill, localBounds, shape->isInverseFillType());
}

void MeasureContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions&,
                               const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  auto rect = Rect::MakeWH(image->width(), image->height());
  addLocalBounds(state, fill, rect);
}

void MeasureContext::drawImageRect(std::shared_ptr<Image>, const Rect& rect, const SamplingOptions&,
                                   const MCState& state, const Fill& fill) {
  addLocalBounds(state, fill, rect);
}

void MeasureContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                      const MCState& state, const Fill& fill,
                                      const Stroke* stroke) {
  auto localBounds = glyphRunList->getBounds();
  if (stroke) {
    stroke->applyToBounds(&localBounds);
  }
  addLocalBounds(state, fill, localBounds);
}

void MeasureContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> imageFilter, const MCState& state,
                               const Fill& fill) {
  DEBUG_ASSERT(picture != nullptr);
  auto deviceBounds = picture->getBounds(&state.matrix);
  if (imageFilter) {
    deviceBounds = imageFilter->filterBounds(deviceBounds);
  }
  addDeviceBounds(state.clip, fill, deviceBounds, picture->hasUnboundedFill());
}

void MeasureContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, state);
}

void MeasureContext::addLocalBounds(const MCState& state, const Fill& fill, const Rect& localBounds,
                                    bool unbounded) {
  auto deviceBounds = state.matrix.mapRect(localBounds);
  addDeviceBounds(state.clip, fill, deviceBounds, unbounded);
}

void MeasureContext::addDeviceBounds(const Path& clip, const Fill& fill, const Rect& deviceBounds,
                                     bool unbounded) {
  if (fill.nothingToDraw()) {
    return;
  }
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
