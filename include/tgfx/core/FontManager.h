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
 * A collection of font styles. There may be multiple typefaces corresponding to different font
 * styles under the same family name.
 * This is an abstract class that users need to implement.
 */
class FontStyleSet {
 public:
  virtual ~FontStyleSet() = default;

  /**
   * Get the number of font styles
   */
  virtual size_t count() = 0;

  /**
   * Get the font style at the specified index
   */
  virtual void getStyle(size_t index, FontStyle* style, std::string* name) = 0;

  /**
   * Create a typeface for the specified font style index
   */
  virtual std::shared_ptr<Typeface> createTypeface(size_t index) = 0;

  /**
   * Match a typeface based on the font style
   */
  virtual std::shared_ptr<Typeface> matchStyle(const FontStyle& style) = 0;

  /**
   * Create an empty font style set
   */
  static std::shared_ptr<FontStyleSet> CreateEmpty();

 protected:
  std::shared_ptr<Typeface> matchStyleCSS3(const FontStyle& pattern);
};

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
   * Get the number of font families
   */
  size_t countFamilies() const;

  /**
   * Get the name of the font family at the given index
   */
  std::string getFamilyName(size_t index) const;

  /**
   * Create a set of font styles for the given family index
   */
  std::shared_ptr<FontStyleSet> createStyleSet(size_t index) const;

  /**
   * Match a font family name and return a set of font styles
   */
  std::shared_ptr<FontStyleSet> matchFamily(const std::string& familyName) const;

  /**
   * Match a font family name and style, and return the corresponding Typeface
   */
  std::shared_ptr<Typeface> matchFamilyStyle(const std::string& familyName, FontStyle style) const;

  /**
   * Match a font family name, style, character, and language, and return the corresponding Typeface
   */
  std::shared_ptr<Typeface> matchFamilyStyleCharacter(const std::string& familyName,
                                                      FontStyle style,
                                                      const std::vector<std::string>& bcp47s,
                                                      Unichar character) const;

 private:
  virtual size_t onCountFamilies() const = 0;
  virtual std::string onGetFamilyName(size_t index) const = 0;
  virtual std::shared_ptr<FontStyleSet> onCreateStyleSet(size_t index) const = 0;
  virtual std::shared_ptr<FontStyleSet> onMatchFamily(const std::string& familyName) const = 0;
  virtual std::shared_ptr<Typeface> onMatchFamilyStyle(const std::string& familyName,
                                                       FontStyle style) const = 0;
  virtual std::shared_ptr<Typeface> onMatchFamilyStyleCharacter(
      const std::string& familyName, FontStyle style, const std::vector<std::string>& bcp47s,
      Unichar character) const = 0;
};

}  // namespace tgfx