/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <cstddef>
#include <memory>
#include <string>
#include "tgfx/core/FontStyle.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
 * FontManager provides functionality to enumerate Typefaces and match them based on FontStyle.
 */
class FontManager {
 public:
  /**
   * Destructor for FontManager
   */
  virtual ~FontManager() = default;

  /**
   * Match a font family name and style, and return the corresponding Typeface
   */
  std::shared_ptr<Typeface> matchTypeface(const std::string& familyName, FontStyle style) const;

  /**
   * Match a font family name, style, character, and language, and return the corresponding Typeface
   */
  std::shared_ptr<Typeface> getFallbackTypeface(const std::string& familyName, FontStyle style,
                                                Unichar character) const;

 private:
  virtual std::shared_ptr<Typeface> onMatchTypeface(const std::string& familyName,
                                                    FontStyle style) const = 0;
  virtual std::shared_ptr<Typeface> onGetFallbackTypeface(const std::string& familyName,
                                                          FontStyle style,
                                                          Unichar character) const = 0;
};

}  // namespace tgfx