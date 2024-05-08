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

#include "LayerUnrollContext.h"
#include "core/PathRef.h"
#include "utils/Log.h"

namespace tgfx {
using namespace pk;

LayerUnrollContext::LayerUnrollContext(DrawContext* drawContext, FillStyle fillStyle,
                                       std::shared_ptr<ImageFilter> filter)
    : drawContext(drawContext), fillStyle(std::move(fillStyle)), imageFilter(std::move(filter)) {
}

void LayerUnrollContext::clear() {
}

void LayerUnrollContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  if (!imageFilter) {
    drawContext->drawRect(rect, state, merge(style));
    unrolled = true;
  }
}

void LayerUnrollContext::drawRRect(const RRect& rRect, const MCState& state,
                                   const FillStyle& style) {
  if (!imageFilter) {
    drawContext->drawRRect(rRect, state, merge(style));
    unrolled = true;
  }
}

void LayerUnrollContext::drawPath(const Path& path, const MCState& state, const FillStyle& style,
                                  const Stroke* stroke) {
  if (imageFilter) {
    return;
  }
  auto& skPath = PathRef::ReadAccess(path);
  if (skPath.hasMultipleContours()) {
    // If the path has multiple contours, they may overlap, so we can't unroll it.
    return;
  }
  drawContext->drawPath(path, state, merge(style), stroke);
  unrolled = true;
}

void LayerUnrollContext::drawImageRect(std::shared_ptr<Image> image,
                                       const SamplingOptions& sampling, const Rect& rect,
                                       const MCState& mcState, const FillStyle& style) {
  auto state = mcState;
  if (imageFilter) {
    image = image->makeSubset(rect);
    // the rect should always be contained in the image bounds.
    DEBUG_ASSERT(image != nullptr);
    Point offset = Point::Zero();
    image = image->makeWithFilter(imageFilter, &offset);
    if (image == nullptr) {
      LOGE("LayerUnrollContext::drawImageRect() Failed to apply filter to image!");
      return;
    }
    state.matrix.preTranslate(offset.x, offset.y);
  }
  drawContext->drawImageRect(std::move(image), sampling, rect, state, merge(style));
  unrolled = true;
}

void LayerUnrollContext::drawGlyphRun(GlyphRun glyphRun, const MCState& state,
                                      const FillStyle& style, const Stroke* stroke) {
  if (!imageFilter) {
    // We presume that within a single GlyphRun, glyphs usually do not overlap.
    drawContext->drawGlyphRun(std::move(glyphRun), state, merge(style), stroke);
    unrolled = true;
  }
}

void LayerUnrollContext::drawLayer(std::shared_ptr<Picture> picture, const MCState& state,
                                   const FillStyle& style, std::shared_ptr<ImageFilter> filter) {
  filter = ImageFilter::Compose(filter, imageFilter);
  drawContext->drawLayer(std::move(picture), state, merge(style), std::move(filter));
  unrolled = true;
}

FillStyle LayerUnrollContext::merge(const FillStyle& style) {
  auto newStyle = style;
  newStyle.color.alpha *= fillStyle.color.alpha;
  newStyle.blendMode = fillStyle.blendMode;
  newStyle.colorFilter = ColorFilter::Compose(style.colorFilter, fillStyle.colorFilter);
  return newStyle;
}
}  // namespace tgfx
