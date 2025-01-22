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
#include "tgfx/core/FontManager.h"
#include "tgfx/core/FontStyle.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
 * A custom FontManager implementation for font style matching. Users only need to override the
 * FontManagerCustom::FontLoader::loadFonts method.
 *
 * Example usage:
 * class FontLoaderTest : public FontManagerCustom::FontLoader {
 *  public:
 *    void loadFonts(std::vector<std::shared_ptr<FontStyleSetCustom>>& families) const override {
 *      auto family = std::make_shared<FontStyleSetCustom>("Noto Sans SC");
 *      auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
 *      typeface->setStyle(FontStyle::Normal());
 *      family->appendTypeface(typeface);
 *      families.push_back(family);
 *    }
 * };
 *
 * int Sample() {
 *   FontLoaderTest loader;
 *   std::shared_ptr<FontManagerCustom> fontManager = std::make_shared<FontManagerCustom>(loader);
 *   SVGDOMOptions options;
 *   options.fontManager = fontManager;
 *   auto SVGDom = SVGDOM::Make(*stream, options);
 * }
 */

class FontStyleSetCustom : public FontStyleSet {
 public:
  explicit FontStyleSetCustom(std::string familyName);

  void appendTypeface(std::shared_ptr<Typeface> typeface);

  size_t count() override;

  void getStyle(size_t index, FontStyle* style, std::string* name) override;

  std::shared_ptr<Typeface> createTypeface(size_t index) override;

  std::shared_ptr<Typeface> matchStyle(const FontStyle& style) override;

  std::string getFamilyName();

 private:
  std::vector<std::shared_ptr<Typeface>> styles;
  std::string familyName;
};

class FontManagerCustom : public FontManager {
 public:
  class FontLoader {
   public:
    virtual ~FontLoader() = default;
    virtual void loadFonts(std::vector<std::shared_ptr<FontStyleSetCustom>>& families) const = 0;
  };

  explicit FontManagerCustom(const FontLoader& loader);

 protected:
  size_t onCountFamilies() const override;
  std::string onGetFamilyName(size_t index) const override;
  std::shared_ptr<FontStyleSet> onCreateStyleSet(size_t index) const override;
  std::shared_ptr<FontStyleSet> onMatchFamily(const std::string& familyName) const override;
  std::shared_ptr<Typeface> onMatchFamilyStyle(const std::string& familyName,
                                               FontStyle style) const override;
  std::shared_ptr<Typeface> onMatchFamilyStyleCharacter(const std::string& familyName,
                                                        FontStyle style,
                                                        const std::vector<std::string>& bcp47s,
                                                        Unichar character) const override;

 private:
  std::vector<std::shared_ptr<FontStyleSetCustom>> families;
  std::shared_ptr<FontStyleSet> defaultFamily;
};

}  // namespace tgfx