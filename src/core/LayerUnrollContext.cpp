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

LayerUnrollContext::LayerUnrollContext(DrawContext* drawContext, Fill fill)
    : drawContext(drawContext), layerFill(std::move(fill)) {
}

void LayerUnrollContext::drawFill(const MCState& state, const Fill& fill) {
  drawContext->drawFill(state, merge(fill));
  unrolled = true;
}

void LayerUnrollContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  drawContext->drawRect(rect, state, merge(fill));
  unrolled = true;
}

void LayerUnrollContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill) {
  drawContext->drawRRect(rRect, state, merge(fill));
  unrolled = true;
}

void LayerUnrollContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                   const Fill& fill) {
  // The shape might have multiple contours and could overlap, so we can't unroll it directly.
  if (fill.isOpaque() && layerFill.isOpaque()) {
    drawContext->drawShape(std::move(shape), state, merge(fill));
    unrolled = true;
  }
}

void LayerUnrollContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                   const MCState& state, const Fill& fill) {
  drawContext->drawImage(std::move(image), sampling, state, merge(fill));
  unrolled = true;
}

void LayerUnrollContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                       const SamplingOptions& sampling, const MCState& state,
                                       const Fill& fill) {
  drawContext->drawImageRect(std::move(image), rect, sampling, state, merge(fill));
  unrolled = true;
}

void LayerUnrollContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                          const MCState& state, const Fill& fill,
                                          const Stroke* stroke) {
  // We assume that glyphs within a single GlyphRunList usually do not overlap.
  drawContext->drawGlyphRunList(std::move(glyphRunList), state, merge(fill), stroke);
  unrolled = true;
}

void LayerUnrollContext::drawLayer(std::shared_ptr<Picture> picture,
                                   std::shared_ptr<ImageFilter> filter, const MCState& state,
                                   const Fill& fill) {
  drawContext->drawLayer(std::move(picture), std::move(filter), state, merge(fill));
  unrolled = true;
}

void LayerUnrollContext::drawPicture(std::shared_ptr<Picture>, const MCState&) {
}

Fill LayerUnrollContext::merge(const Fill& fill) {
  auto newFill = fill;
  newFill.color.alpha *= layerFill.color.alpha;
  newFill.blendMode = layerFill.blendMode;
  newFill.colorFilter = ColorFilter::Compose(fill.colorFilter, layerFill.colorFilter);
  return newFill;
}
}  // namespace tgfx
