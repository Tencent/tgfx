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

#pragma once

#include "core/DrawContext.h"

namespace tgfx {
/**
 * A context that applies a transformation to the state before passing it to the draw context.
 */
class TransformContext : public DrawContext {
 public:
  enum class Type { None, Matrix, Clip, Both };

  TransformContext(DrawContext* drawContext, const MCState& state);

  Type type() const {
    return _type;
  }

  void drawFill(const Fill& fill) override {
    switch (_type) {
      case Type::None:
      case Type::Matrix:
        drawContext->drawFill(fill.makeWithMatrix(initState.matrix));
        break;
      default:
        auto state = transform({});
        drawContext->drawPath(state.clip, {}, fill.makeWithMatrix(state.matrix));
        break;
    }
  }

  void drawRect(const Rect& rect, const MCState& state, const Fill& fill) override {
    drawContext->drawRect(rect, transform(state), fill);
  }

  void drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                 const Stroke* stroke = nullptr) override {
    drawContext->drawRRect(rRect, transform(state), fill, stroke);
  }

  void drawPath(const Path& path, const MCState& state, const Fill& fill) override {
    drawContext->drawPath(path, transform(state), fill);
  }

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill) override {
    drawContext->drawShape(std::move(shape), transform(state), fill);
  }

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Fill& fill) override {
    drawContext->drawImage(std::move(image), sampling, transform(state), fill);
  }

  void drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                     const SamplingOptions& sampling, const MCState& state,
                     const Fill& fill) override {
    drawContext->drawImageRect(std::move(image), rect, sampling, transform(state), fill);
  }

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                        const Fill& fill, const Stroke* stroke) override {
    drawContext->drawGlyphRunList(std::move(glyphRunList), transform(state), fill, stroke);
  }

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override {
    drawContext->drawPicture(std::move(picture), transform(state));
  }

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const Fill& fill) override {
    drawContext->drawLayer(std::move(picture), std::move(filter), transform(state), fill);
  }

 private:
  Type _type = Type::None;
  DrawContext* drawContext = nullptr;
  MCState initState = {};
  Path lastClip = {};
  Path lastIntersectedClip = {};

  MCState transform(const MCState& state);
};
}  // namespace tgfx
