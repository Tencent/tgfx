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

#include "tgfx/layers/vectors/TextSpan.h"
#include "VectorContext.h"
#include "core/utils/Log.h"

namespace tgfx {

void TextSpan::setTextBlob(std::shared_ptr<TextBlob> value) {
  if (_textBlob == value) {
    return;
  }
  _textBlob = std::move(value);
  invalidateContent();
}

void TextSpan::setPosition(const Point& value) {
  if (_position == value) {
    return;
  }
  _position = value;
  invalidateContent();
}

void TextSpan::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (_textBlob == nullptr) {
    return;
  }
  context->addTextBlob(_textBlob, _position);
}

}  // namespace tgfx
