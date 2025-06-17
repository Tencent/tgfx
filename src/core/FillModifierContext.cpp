/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "FillModifierContext.h"
#include "core/utils/Log.h"

namespace tgfx {

FillModifierContext::FillModifierContext(DrawContext* drawContext, const FillModifier* fillModifier)
    : drawContext(drawContext), fillModifier(fillModifier) {
  DEBUG_ASSERT(drawContext != nullptr);
}

void FillModifierContext::drawFill(const Fill& fill) {
  drawContext->drawFill(fillModifier->modify(fill));
}

void FillModifierContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  drawContext->drawRect(rect, state, fillModifier->modify(fill));
}

void FillModifierContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                                    const Stroke* stroke) {
  drawContext->drawRRect(rRect, state, fillModifier->modify(fill), stroke);
}

void FillModifierContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  drawContext->drawPath(path, state, fillModifier->modify(fill));
}

void FillModifierContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                    const Fill& fill) {
  drawContext->drawShape(std::move(shape), state, fillModifier->modify(fill));
}

void FillModifierContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                        const SamplingOptions& sampling, const MCState& state,
                                        const Fill& fill) {
  drawContext->drawImageRect(std::move(image), rect, sampling, state, fillModifier->modify(fill));
}

void FillModifierContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                           const MCState& state, const Fill& fill,
                                           const Stroke* stroke) {
  drawContext->drawGlyphRunList(std::move(glyphRunList), state, fillModifier->modify(fill), stroke);
}

void FillModifierContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  picture->playback(drawContext, state, fillModifier);
}

void FillModifierContext::drawLayer(std::shared_ptr<Picture> picture,
                                    std::shared_ptr<ImageFilter> filter, const MCState& state,
                                    const Fill& fill) {
  drawContext->drawLayer(std::move(picture), std::move(filter), state, fillModifier->modify(fill));
}
}  // namespace tgfx
