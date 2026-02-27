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

#include "LayerUnrollContext.h"

namespace tgfx {
LayerUnrollContext::LayerUnrollContext(DrawContext* drawContext, Brush layerBrush)
    : drawContext(drawContext), layerBrush(std::move(layerBrush)) {
}

void LayerUnrollContext::drawFill(const Brush& brush) {
  drawContext->drawFill(mergeBrush(brush));
  unrolled = true;
}

void LayerUnrollContext::drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                                  const Stroke* stroke) {
  drawContext->drawRect(rect, state, mergeBrush(brush), stroke);
  unrolled = true;
}

void LayerUnrollContext::drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                                   const Stroke* stroke) {
  drawContext->drawRRect(rRect, state, mergeBrush(brush), stroke);
  unrolled = true;
}

void LayerUnrollContext::drawPath(const Path& path, const MCState& state, const Brush& brush) {
  drawContext->drawPath(path, state, mergeBrush(brush));
  unrolled = true;
}

void LayerUnrollContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                   const Brush& brush, const Stroke* stroke) {
  drawContext->drawShape(std::move(shape), state, mergeBrush(brush), stroke);
  unrolled = true;
}

void LayerUnrollContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                   const MCState& state, const Brush& brush) {
  drawContext->drawImage(std::move(image), sampling, state, mergeBrush(brush));
  unrolled = true;
}

void LayerUnrollContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                       const Rect& dstRect, const SamplingOptions& sampling,
                                       const MCState& state, const Brush& brush,
                                       SrcRectConstraint constraint) {
  drawContext->drawImageRect(std::move(image), srcRect, dstRect, sampling, state, mergeBrush(brush),
                             constraint);
  unrolled = true;
}

void LayerUnrollContext::drawTextBlob(std::shared_ptr<TextBlob> textBlob, const MCState& state,
                                      const Brush& brush, const Stroke* stroke) {
  drawContext->drawTextBlob(std::move(textBlob), state, mergeBrush(brush), stroke);
  unrolled = true;
}

void LayerUnrollContext::drawLayer(std::shared_ptr<Picture> picture,
                                   std::shared_ptr<ImageFilter> filter, const MCState& state,
                                   const Brush& brush) {
  drawContext->drawLayer(std::move(picture), std::move(filter), state, mergeBrush(brush));
  unrolled = true;
}

void LayerUnrollContext::drawPicture(std::shared_ptr<Picture>, const MCState&) {
}

Brush LayerUnrollContext::mergeBrush(const Brush& brush) const {
  auto newBrush = brush;
  newBrush.color.alpha *= layerBrush.color.alpha;
  newBrush.blendMode = layerBrush.blendMode;
  newBrush.colorFilter = ColorFilter::Compose(brush.colorFilter, layerBrush.colorFilter);
  return newBrush;
}
}  // namespace tgfx
