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

#include "tgfx/core/Font.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/TextAlign.h"

namespace tgfx {
/**
 * A layer that provides simple layout and rendering of a plain text.
 */
class TextLayer : public Layer {
 public:
  /**
   * Creates a new text layer.
   */
  static std::shared_ptr<TextLayer> Make();

  LayerType type() const override {
    return LayerType::Text;
  }

  /**
   * Returns the text to be displayed in the text layer. Lines are separated by the '\n'.
   */
  const std::string& text() const {
    return _text;
  }

  /**
   * Sets the text to be displayed in the text layer.
   */
  void setText(const std::string& text);

  /**
   * Returns the color of the text. The default color is opaque white.
   */
  const Color& textColor() const {
    return _textColor;
  }

  /**
   * Sets the color of the text.
   */
  void setTextColor(const Color& color);

  /**
   * Returns the font used to render the text.
   */
  const Font& font() const {
    return _font;
  }

  /**
   * Sets the font used to render the text.
   */
  void setFont(const Font& font);

  /**
   * Returns the layout width of the text, used for alignment or wrapping. The default value is 100.
   * If set to 0 or less, the text will be rendered without alignment or wrapping.
   */
  float textWidth() const {
    return _textWidth;
  }

  /**
   * Sets the layout width of the text.
   */
  void setTextWidth(float width);

  /**
   * Determines how the text should be horizontally aligned within the text width. The default value
   * is TextAlign::Left.
   */
  TextAlign textAlign() const {
    return _textAlign;
  }

  /**
   * Sets how the text should be horizontally aligned within the text width.
   */
  void setTextAlign(TextAlign align);

  /**
   * Returns whether the text should be wrapped to fit within the text width. The default value is
   * false.
   */
  bool wrapped() const {
    return _wrapped;
  }

  /**
   * Sets whether the text should be wrapped to fit within the text width.
   */
  void setWrapped(bool value);

 protected:
  TextLayer() = default;

  std::unique_ptr<LayerContent> onUpdateContent() override;

 private:
  std::string _text;
  Color _textColor = Color::White();
  Font _font = {};
  float _textWidth = 100;
  TextAlign _textAlign = TextAlign::Left;
  bool _wrapped = false;
};
}  // namespace tgfx
