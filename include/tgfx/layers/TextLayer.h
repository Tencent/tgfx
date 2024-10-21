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
#include "tgfx/layers/HorizontalAlign.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/VerticalAlign.h"

namespace tgfx {
/**
 * A layer that provides simple layout and rendering of a plain text.
 */
class TextLayer : public Layer {
 public:
  /**
   * Creates a new text layer with the specified layout width and height.
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
   * Returns the layout width of the text, used for horizontal alignment or wrapping. The default
   * value is 0, meaning the text will be rendered without any horizontal alignment or wrapping.
   */
  float width() const {
    return _width;
  }

  /**
   * Sets the layout width of the text.
   */
  void setWidth(float width);

  /**
   * Returns the layout height of the text, used for vertical alignment. The default value is 0,
   * meaning the text will be rendered without any vertical alignment.
   */
  float height() const {
    return _height;
  }

  /**
   * Sets the layout height of the text.
   */
  void setHeight(float height);

  /**
   * Specifies how the text should be horizontally aligned within the layout width. The default is
   * HorizontalAlign::Left. This setting is ignored if the layout width is 0.
   */
  HorizontalAlign horizontalAlign() const {
    return _horizontalAlign;
  }

  /**
   * Sets how the text should be horizontally aligned within the layout width.
   */
  void setHorizontalAlign(HorizontalAlign align);

  /**
   * Specifies how the text should be vertically aligned within the layout height. The default is
   * VerticalAlign::Top. This setting is ignored if the layout height is 0.
   */
  VerticalAlign verticalAlign() const {
    return _verticalAlign;
  }

  /**
   * Sets how the text should be vertically aligned within the layout height.
   */
  void setVerticalAlign(VerticalAlign align);

  /**
   * Returns whether the text should be wrapped to fit within the text width. The default value is
   * false. This setting is ignored if the layout width is 0.
   */
  bool autoWrap() const {
    return _autoWrap;
  }

  /**
   * Sets whether the text should be wrapped to fit within the text width.
   */
  void setAutoWrap(bool value);

 protected:
  TextLayer() = default;

  std::unique_ptr<LayerContent> onUpdateContent() override;

 private:
  std::string _text;
  Color _textColor = Color::White();
  Font _font = {};
  float _width = 0;
  float _height = 0;
  HorizontalAlign _horizontalAlign = HorizontalAlign::Left;
  VerticalAlign _verticalAlign = VerticalAlign::Top;
  bool _autoWrap = false;
};
}  // namespace tgfx
