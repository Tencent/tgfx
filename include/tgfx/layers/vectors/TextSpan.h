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
 * TextSpan represents a text blob with position. Multiple TextSpans can be combined with
 * VectorGroup to create rich text with different styles.
 */
class TextSpan : public VectorElement {
 public:
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

 protected:
  Type type() const override {
    return Type::TextSpan;
  }

  void apply(VectorContext* context) override;

 private:
  std::shared_ptr<TextBlob> _textBlob = nullptr;
  Point _position = {};
};

}  // namespace tgfx
