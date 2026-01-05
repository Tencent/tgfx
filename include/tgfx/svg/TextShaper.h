/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "tgfx/core/TextBlob.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
 * An abstract class for text layout and shaping. This class converts text strings into TextBlob
 * objects. Users can provide their own implementation.
 */
class TextShaper {
 public:
  /**
   * Creates a TextShaper object with the specified fallback typeface list.
   * Provides a basic implementation.
   */
  static std::shared_ptr<TextShaper> Make(
      std::vector<std::shared_ptr<Typeface>> fallbackTypefaceList);

  /**
   * Destroys the TextShaper object.
   */
  virtual ~TextShaper() = default;

  /**
   * Shapes the input text string into a TextBlob object. The typeface parameter is the system font
   * matched based on text attributes, or nullptr if no match is found. The fontSize parameter
   * specifies the font size to use.
   */
  virtual std::shared_ptr<TextBlob> shape(const std::string& text,
                                          std::shared_ptr<Typeface> typeface, float fontSize) = 0;
};

}  // namespace tgfx
