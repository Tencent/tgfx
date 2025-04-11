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

#include <string>
#include "tgfx/core/Rect.h"
namespace tgfx {

struct TypefaceMetrics {
  // These enum values match the values used in the PDF file format.
  enum StyleFlags {
    FixedPitch = 0x00000001,
    Serif = 0x00000002,
    Script = 0x00000008,
    Italic = 0x00000040,
    AllCaps = 0x00010000,
    SmallCaps = 0x00020000,
    ForceBold = 0x00040000
  };

  enum class FontType {
    Type1,
    Type1CID,
    CFF,
    TrueType,
    Other,
  };

  enum FontFlags {
    Variable = 1 << 0,        //!<May be true for Type1, CFF, or TrueType fonts.
    NotEmbeddable = 1 << 1,   //!<May not be embedded.
    NotSubsettable = 1 << 2,  //!<May not be subset.
    AltDataFormat = 1 << 3,   //!<Data compressed. Table access may still work.
  };

  std::string postScriptName;
  StyleFlags style = static_cast<StyleFlags>(0);  // Font style characteristics.
  FontType type = FontType::Other;
  FontFlags flags = static_cast<FontFlags>(0);  // Global font flags.

  int16_t fItalicAngle = 0;  // Counterclockwise degrees from vertical of the
                             // dominant vertical stroke for an Italic face.
  // The following fields are all in font units.
  int16_t fAscent = 0;     // Max height above baseline, not including accents.
  int16_t fDescent = 0;    // Max depth below baseline (negative).
  int16_t fStemV = 0;      // Thickness of dominant vertical stem.
  int16_t fCapHeight = 0;  // Height (from baseline) of top of flat capitals.

  Rect fBBox = {0, 0, 0, 0};  // The bounding box of all glyphs (in font units).
};

}  // namespace tgfx