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

namespace tgfx {
void MeasureContext::clear() {
  bounds.setEmpty();
}

void MeasureContext::drawRect(const Rect& rect, const MCState& state, const FillStyle&) {
  addLocalBounds(state, rect);
}

void MeasureContext::drawRRect(const RRect& rRect, const MCState& state, const FillStyle&) {
  addLocalBounds(state, rRect.rect);
}

void MeasureContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                               const FillStyle&) {
  auto localBounds = shape->getBounds(state.matrix.getMaxScale());
  addLocalBounds(state, localBounds, shape->isInverseFillType());
}

void MeasureContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions&,
                               const MCState& state, const FillStyle&) {
  if (image == nullptr) {
    return;
  }
  auto rect = Rect::MakeWH(image->width(), image->height());
  addLocalBounds(state, rect);
}

void MeasureContext::drawImageRect(std::shared_ptr<Image>, const Rect& rect, const SamplingOptions&,
                                   const MCState& state, const FillStyle&) {
  addLocalBounds(state, rect);
}

void MeasureContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                      const Stroke* stroke, const MCState& state,
                                      const FillStyle&) {
  auto localBounds = glyphRunList->getBounds(state.matrix.getMaxScale());
  if (stroke) {
    stroke->applyToBounds(&localBounds);
  }
  addLocalBounds(state, localBounds);
}

void MeasureContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> imageFilter, const MCState& state,
                               const FillStyle&) {
  if (picture == nullptr) {
    return;
  }
  auto deviceBounds = picture->getBounds(&state.matrix);
  if (imageFilter) {
    deviceBounds = imageFilter->filterBounds(deviceBounds);
  }
  addDeviceBounds(state.clip, deviceBounds);
}

void MeasureContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  if (picture != nullptr) {
    picture->playback(this, state);
  }
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
