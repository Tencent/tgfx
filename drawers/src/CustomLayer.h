/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <utility>
#include "tgfx/layers/Layer.h"

namespace drawers {

class CustomLayerContent : public tgfx::LayerContent {
 public:
  explicit CustomLayerContent(std::shared_ptr<tgfx::TextBlob> textBlob)
      : _textBlob(std::move(textBlob)){};
  tgfx::Rect getBounds() const override {
    return _textBlob->getBounds();
  }

  void draw(tgfx::Canvas* canvas, const tgfx::Paint& paint) const override {
    auto textPaint = paint;
    textPaint.setColor(tgfx::Color::Black());
    canvas->drawTextBlob(_textBlob, 0, 0, textPaint);
  }

  bool hitTestPoint(float localX, float localY, bool pixelHitTest) override {
    auto bounds = _textBlob->getBounds();
    return bounds.contains(localX, localY);
  }

 private:
  std::shared_ptr<tgfx::TextBlob> _textBlob;
};

class CustomLayer : public tgfx::Layer {
 public:
  static std::shared_ptr<CustomLayer> Make();
  void setText(const std::string& text) {
    if (text == _text) {
      return;
    }
    _text = text;
    invalidateContent();
  }

  std::string text() const {
    return _text;
  }

  void setFont(const tgfx::Font& font) {
    if (font == _font) {
      return;
    }
    _font = font;
    invalidateContent();
  }

  tgfx::Font font() const {
    return _font;
  }

  std::unique_ptr<tgfx::LayerContent> onUpdateContent() override;

 protected:
  CustomLayer() = default;

 private:
  std::string _text;
  tgfx::Font _font;
};

}  // namespace drawers
