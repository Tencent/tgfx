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

#include "tgfx/core/Font.h"

namespace tgfx {
Font::Font() : Font(Typeface::MakeDefault()) {
}

Font::Font(std::shared_ptr<Typeface> tf, float textSize) {
  setTypeface(std::move(tf));
  setSize(textSize);
}

Font Font::makeWithSize(float newSize) const {
  auto newFont = *this;
  newFont.setSize(newSize);
  return newFont;
}

void Font::setTypeface(std::shared_ptr<Typeface> newTypeface) {
  if (newTypeface == nullptr) {
    typeface = Typeface::MakeDefault();
  } else {
    typeface = std::move(newTypeface);
  }
}

void Font::setSize(float newSize) {
  if (newSize <= 0) {
    size = 12.0f;
  } else {
    size = newSize;
  }
}
}  // namespace tgfx