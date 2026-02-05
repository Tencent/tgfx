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

#include "tgfx/layers/vectors/Text.h"
#include "VectorContext.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<Text> Text::Make() {
  return std::shared_ptr<Text>(new Text());
}

void Text::setTextBlob(std::shared_ptr<TextBlob> value) {
  if (_textBlob == value) {
    return;
  }
  _textBlob = std::move(value);
  invalidateContent();
}

void Text::setPosition(const Point& value) {
  if (_position == value) {
    return;
  }
  _position = value;
  invalidateContent();
}

void Text::setAnchors(std::vector<Point> value) {
  if (_anchors == value) {
    return;
  }
  _anchors = std::move(value);
  invalidateContent();
}

void Text::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (_textBlob == nullptr) {
    return;
  }
  if (!_anchors.empty()) {
    size_t glyphCount = 0;
    for (auto run : *_textBlob) {
      glyphCount += run.glyphCount;
    }
    if (_anchors.size() != glyphCount) {
      LOGE("Text::apply: anchors size (%zu) does not match glyph count (%zu)", _anchors.size(),
           glyphCount);
    }
  }
  context->addTextBlob(_textBlob, _position, _anchors);
}

}  // namespace tgfx
