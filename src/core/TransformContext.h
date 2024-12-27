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
  /**
   * Creates a new transform context that applies the given matrix to the state. Returns nullptr if
   * the matrix is identity or the drawContext is nullptr.
   */
  static std::unique_ptr<TransformContext> Make(DrawContext* drawContext, const Matrix& matrix);

  /**
   * Creates a new transform context that applies the given matrix and clip to the state. Returns
   * nullptr if the matrix is identity and the clip is wide open, or the drawContext is nullptr, or
   * the clip is empty.
   */
  static std::unique_ptr<TransformContext> Make(DrawContext* drawContext, const Matrix& matrix,
                                                const Path& clip);

  explicit TransformContext(DrawContext* drawContext) : drawContext(drawContext) {
  }

  void clear() override {
    drawContext->clear();
  }

  void drawRect(const Rect& rect, const MCState& state, const FillStyle& style) override {
    drawContext->drawRect(rect, transform(state), style);
  }

  void drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) override {
    drawContext->drawRRect(rRect, transform(state), style);
  }

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                 const FillStyle& style) override {
    drawContext->drawShape(std::move(shape), transform(state), style);
  }

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const FillStyle& style) override {
    drawContext->drawImage(std::move(image), sampling, transform(state), style);
  }

  void drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                     const SamplingOptions& sampling, const MCState& state,
                     const FillStyle& style) override {
    drawContext->drawImageRect(std::move(image), rect, sampling, transform(state), style);
  }

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const Stroke* stroke,
                        const MCState& state, const FillStyle& style) override {
    drawContext->drawGlyphRunList(std::move(glyphRunList), stroke, transform(state), style);
  }

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override {
    drawContext->drawPicture(std::move(picture), transform(state));
  }

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const FillStyle& style) override {
    drawContext->drawLayer(std::move(picture), std::move(filter), transform(state), style);
  }

 protected:
  virtual MCState transform(const MCState& state) = 0;

 private:
  DrawContext* drawContext = nullptr;
};
}  // namespace tgfx
