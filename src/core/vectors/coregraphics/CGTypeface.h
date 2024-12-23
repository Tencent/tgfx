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

#pragma once

#include <CoreText/CoreText.h>
#include "tgfx/core/Typeface.h"

namespace tgfx {
class CGTypeface : public Typeface {
 public:
  static std::shared_ptr<CGTypeface> Make(CTFontRef ctFont, std::shared_ptr<Data> data = nullptr);

  ~CGTypeface() override;

  CTFontRef getCTFont() const {
    return ctFont;
  }

  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  std::string fontFamily() const override;

  std::string fontStyle() const override;

  size_t glyphsCount() const override {
    return static_cast<size_t>(CTFontGetGlyphCount(ctFont));
  }

  int unitsPerEm() const override;

  bool hasColor() const override {
    return _hasColor;
  }

  bool hasOutlines() const override {
    return _hasOutlines;
  }

  GlyphID getGlyphID(Unichar unichar) const override;

  std::shared_ptr<Data> getBytes() const override;

  std::shared_ptr<Data> copyTableData(FontTableTag tag) const override;

 protected:
#ifdef TGFX_USE_GLYPH_TO_UNICODE
  std::vector<Unichar> getGlyphToUnicodeMap() const override;
#endif

 private:
  CGTypeface(CTFontRef ctFont, std::shared_ptr<Data> data);

  uint32_t _uniqueID = 0;
  CTFontRef ctFont = nullptr;
  bool _hasColor = false;
  bool _hasOutlines = true;
  std::shared_ptr<Data> data;
  std::weak_ptr<CGTypeface> weakThis;

  friend class CGScalerContext;
};
}  // namespace tgfx
