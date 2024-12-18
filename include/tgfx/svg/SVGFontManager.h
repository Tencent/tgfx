/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>
#include "tgfx/core/Typeface.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGNode.h"
namespace tgfx {

class SVGFontInfo {
 public:
  struct Hash {
    std::size_t operator()(const SVGFontInfo& info) const {
      return std::hash<int>()(static_cast<int>(info._weight)) ^
             std::hash<int>()(static_cast<int>(info._style));
    }
  };

  SVGFontInfo(SVGFontWeight::Type weight, SVGFontStyle::Type style)
      : _weight(weight), _style(style) {
  }

  bool operator==(const SVGFontInfo& other) const {
    return _weight == other._weight && _style == other._style;
  }

  SVGFontWeight::Type weight() const {
    return _weight;
  }
  SVGFontStyle::Type style() const {
    return _style;
  }

 private:
  SVGFontWeight::Type _weight;
  SVGFontStyle::Type _style;
};

class SVGFontManager {
 public:
  static std::shared_ptr<SVGFontManager> Make(const std::shared_ptr<Typeface>& defaultTypeface);

  ~SVGFontManager() = default;

  void setTypeface(const std::string& fontFamily, SVGFontInfo style,
                   const std::shared_ptr<Typeface>& typeface);

  std::vector<std::string> getFontFamilies() const;

  std::vector<SVGFontInfo> getFontStyles(const std::string& fontFamily) const;

 protected:
  void addFontStyle(const std::string& fontFamily, SVGFontInfo style);

  std::shared_ptr<Typeface> getTypefaceForRender(const std::string& fontFamily,
                                                 SVGFontInfo style) const;

 private:
  explicit SVGFontManager(const std::shared_ptr<Typeface>& typeface);

  std::unordered_map<std::string,
                     std::unordered_map<SVGFontInfo, std::shared_ptr<Typeface>, SVGFontInfo::Hash>>
      typefaceMap;
  std::shared_ptr<Typeface> defaultTypeface;

  friend class SVGRenderContext;
  friend class SVGDOM;
};

}  // namespace tgfx