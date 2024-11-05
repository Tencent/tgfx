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

namespace tgfx {
using namespace pk;

LayerUnrollContext::LayerUnrollContext(DrawContext* drawContext, FillStyle fillStyle)
    : drawContext(drawContext), fillStyle(std::move(fillStyle)) {
}

void LayerUnrollContext::clear() {
}

void LayerUnrollContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  drawContext->drawRect(rect, state, merge(style));
  unrolled = true;
}

void LayerUnrollContext::drawRRect(const RRect& rRect, const MCState& state,
                                   const FillStyle& style) {
  drawContext->drawRRect(rRect, state, merge(style));
  unrolled = true;
}

void LayerUnrollContext::drawPath(const Path& path, const MCState& state, const FillStyle& style,
                                  const Stroke* stroke) {
  // The shape might have multiple contours and could overlap, so we can't unroll it directly.
  if (style.isOpaque() && fillStyle.isOpaque()) {
    drawContext->drawPath(path, state, merge(style), stroke);
    unrolled = true;
  }
}

void LayerUnrollContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                   const MCState& state, const FillStyle& style) {
  drawContext->drawImage(std::move(image), sampling, state, merge(style));
  unrolled = true;
}

void LayerUnrollContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                       const SamplingOptions& sampling, const MCState& state,
                                       const FillStyle& style) {
  drawContext->drawImageRect(std::move(image), rect, sampling, state, merge(style));
  unrolled = true;
}

void LayerUnrollContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                          const MCState& state, const FillStyle& style,
                                          const Stroke* stroke) {
  // We assume that glyphs within a single GlyphRunList usually do not overlap.
  drawContext->drawGlyphRunList(std::move(glyphRunList), state, merge(style), stroke);
  unrolled = true;
}

void LayerUnrollContext::drawLayer(std::shared_ptr<Picture> picture, const MCState& state,
                                   const FillStyle& style, std::shared_ptr<ImageFilter> filter) {
  drawContext->drawLayer(std::move(picture), state, merge(style), std::move(filter));
  unrolled = true;
}

void LayerUnrollContext::drawPicture(std::shared_ptr<Picture>, const MCState&) {
}

FillStyle LayerUnrollContext::merge(const FillStyle& style) {
  auto newStyle = style;
  newStyle.color.alpha *= fillStyle.color.alpha;
  newStyle.blendMode = fillStyle.blendMode;
  newStyle.colorFilter = ColorFilter::Compose(style.colorFilter, fillStyle.colorFilter);
  return newStyle;
}
}  // namespace tgfx
