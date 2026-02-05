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
   * Creates a new Text instance.
   */
  static std::shared_ptr<Text> Make();
  /**
   * Returns the text blob to render.
   */
  std::shared_ptr<TextBlob> textBlob() const {
    return _textBlob;
  }

  /**
   * Sets the text blob to render.
   */
  void setTextBlob(std::shared_ptr<TextBlob> value);

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

  /**
   * Sets the anchor offsets for each glyph. The array length should match the total glyph count
   * of the text blob. If shorter, missing entries default to (0, 0). If longer, extra entries are
   * ignored. A warning is logged if the length does not match.
   */
  void setAnchors(std::vector<Point> value);

 protected:
  Type type() const override {
    return Type::Text;
  }

  void apply(VectorContext* context) override;

 protected:
  Text() = default;

 private:
  std::shared_ptr<TextBlob> _textBlob = nullptr;
  Point _position = {};
  std::vector<Point> _anchors = {};
};

}  // namespace tgfx
