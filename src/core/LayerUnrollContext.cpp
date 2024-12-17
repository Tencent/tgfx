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
#include "core/utils/Log.h"
#include "core/utils/Profiling.h"

namespace tgfx {
using namespace pk;

LayerUnrollContext::LayerUnrollContext(DrawContext* drawContext, FillStyle fillStyle,
                                       std::shared_ptr<ImageFilter> imageFilter)
    : drawContext(drawContext), fillStyle(std::move(fillStyle)),
      imageFilter(std::move(imageFilter)) {
}

void LayerUnrollContext::clear() {
}

void LayerUnrollContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  TRACE_EVENT;
  if (imageFilter == nullptr) {
    drawContext->drawRect(rect, state, merge(style));
    unrolled = true;
  }
}

void LayerUnrollContext::drawRRect(const RRect& rRect, const MCState& state,
                                   const FillStyle& style) {
  TRACE_EVENT;
  if (imageFilter == nullptr) {
    drawContext->drawRRect(rRect, state, merge(style));
    unrolled = true;
  }
}

void LayerUnrollContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                   const FillStyle& style) {
  TRACE_EVENT;
  // The shape might have multiple contours and could overlap, so we can't unroll it directly.
  if (imageFilter == nullptr && style.isOpaque() && fillStyle.isOpaque()) {
    drawContext->drawShape(std::move(shape), state, merge(style));
    unrolled = true;
  }
}

void LayerUnrollContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                   const MCState& state, const FillStyle& style) {
  TRACE_EVENT;
  if (imageFilter != nullptr) {
    drawImageWidthFilter(std::move(image), sampling, state, style);
    return;
  }
  drawContext->drawImage(std::move(image), sampling, state, merge(style));
  unrolled = true;
}

void LayerUnrollContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                       const SamplingOptions& sampling, const MCState& state,
                                       const FillStyle& style) {
  TRACE_EVENT;
  if (imageFilter != nullptr) {
    image = image->makeSubset(rect);
    // the rect should always be contained in the image bounds.
    DEBUG_ASSERT(image != nullptr);
    drawImageWidthFilter(std::move(image), sampling, state, style);
    return;
  }
  drawContext->drawImageRect(std::move(image), rect, sampling, state, merge(style));
  unrolled = true;
}

void LayerUnrollContext::drawImageWidthFilter(std::shared_ptr<Image> image,
                                              const SamplingOptions& sampling, const MCState& state,
                                              const FillStyle& style) {
  TRACE_EVENT;
  if (style.colorFilter || style.maskFilter) {
    return;
  }
  if (image->isAlphaOnly()) {
    if (style.shader || !style.color.isOpaque()) {
      return;
    }
  }
  auto& viewMatrix = state.matrix;
  if (!viewMatrix.isTranslate()) {
    return;
  }
  auto clipOffset = Point::Zero();
  auto& clip = state.clip;
  if (clip.isInverseFillType()) {
    if (!clip.isEmpty()) {
      return;
    }
  } else {
    auto clipRect = Rect::MakeEmpty();
    if (!clip.isRect(&clipRect)) {
      return;
    }
    clipRect.offset(-viewMatrix.getTranslateX(), -viewMatrix.getTranslateY());
    image = image->makeSubset(clipRect);
    if (image == nullptr) {
      return;
    }
    clipOffset = Point::Make(clipRect.left, clipRect.top);
  }
  auto offset = Point::Zero();
  image = image->makeWithFilter(std::move(imageFilter), &offset);
  if (image == nullptr) {
    return;
  }
  auto mcState = state;
  mcState.matrix.preTranslate(offset.x + clipOffset.x, offset.y + clipOffset.y);
  drawContext->drawImage(std::move(image), sampling, mcState, merge(style));
  unrolled = true;
}

void LayerUnrollContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                          const MCState& state, const FillStyle& style,
                                          const Stroke* stroke) {
  TRACE_EVENT;
  if (imageFilter == nullptr) {
    // We assume that glyphs within a single GlyphRunList usually do not overlap.
    drawContext->drawGlyphRunList(std::move(glyphRunList), state, merge(style), stroke);
    unrolled = true;
  }
}

void LayerUnrollContext::drawLayer(std::shared_ptr<Picture> picture, const MCState& state,
                                   const FillStyle& style, std::shared_ptr<ImageFilter> filter) {
  TRACE_EVENT;
  if (imageFilter == nullptr) {
    drawContext->drawLayer(std::move(picture), state, merge(style), std::move(filter));
    unrolled = true;
  }
}

void LayerUnrollContext::drawPicture(std::shared_ptr<Picture>, const MCState&) {
}

FillStyle LayerUnrollContext::merge(const FillStyle& style) {
  TRACE_EVENT;
  auto newStyle = style;
  newStyle.color.alpha *= fillStyle.color.alpha;
  newStyle.blendMode = fillStyle.blendMode;
  newStyle.colorFilter = ColorFilter::Compose(style.colorFilter, fillStyle.colorFilter);
  return newStyle;
}
}  // namespace tgfx
