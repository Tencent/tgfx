/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/core/Point.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Text represents a text blob with position. Multiple Text elements can be combined with
 * VectorGroup to create rich text with different styles.
 */
class Text : public VectorElement {
 public:
  /**
   * Creates a new Text instance with the specified text blob and optional anchor offsets.
   * @param textBlob The text blob to render. If null, returns nullptr.
   * @param anchors Optional anchor offsets for each glyph. These offsets are relative to each
   * glyph's default anchor point at (advance * 0.5, 0). If the array length does not match the
   * glyph count, it will be resized (padded with zeros or truncated) and a warning is logged.
   * @return A new Text instance, or nullptr if textBlob is null.
   */
  static std::shared_ptr<Text> Make(std::shared_ptr<TextBlob> textBlob,
                                    std::vector<Point> anchors = {});

  /**
   * Returns the text blob to render.
   */
  std::shared_ptr<TextBlob> textBlob() const {
    return _textBlob;
  }

  /**
   * Returns the position of the text blob.
   */
  Point position() const {
    return _position;
  }

  /**
   * Sets the position of the text blob.
   */
  void setPosition(const Point& value);

  /**
   * Returns the anchor offsets for each glyph. These offsets are relative to each glyph's default
   * anchor point at (advance * 0.5, 0). If empty, no additional offset is applied.
   */
  const std::vector<Point>& anchors() const {
    return _anchors;
  }

 protected:
  Type type() const override {
    return Type::Text;
  }

  void apply(VectorContext* context) override;

 protected:
  Text(std::shared_ptr<TextBlob> textBlob, std::vector<Point> anchors)
      : _textBlob(std::move(textBlob)), _anchors(std::move(anchors)) {
  }

 private:
  std::shared_ptr<TextBlob> _textBlob = nullptr;
  Point _position = {};
  std::vector<Point> _anchors = {};
};

}  // namespace tgfx
