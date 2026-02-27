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

#include <string>

namespace tgfx {

class AdvancedTypefaceInfo {
 public:
  /**
   * PostScript name.
   */
  std::string postScriptName;
  /**
   * Font type.
   */
  enum class FontType {
    Type1,
    Type1CID,
    CFF,
    TrueType,
    Other,
  };
  FontType type = FontType::Other;

  /**
   * Font Descriptor Flags, aligned with the PDF specification's fontDescriptorFlags property.
   */
  enum StyleFlags {
    // All characters in the font have the same width.
    FixedPitch = 0x00000001,
    // The font is a serif font (e.g., Times New Roman).
    Serif = 0x00000002,
    // The font is a symbolic font (e.g., Wingdings).
    Symbolic = 0x00000004,
    // The font is italic.
    Italic = 0x00000040,
    // The font uses all uppercase letters.
    AllCaps = 0x00010000,
    // The font uses small capital letters.
    SmallCaps = 0x00020000,
    // Forces bold display even if the font is not marked as bold.
    ForceBold = 0x00040000
  };
  StyleFlags style = static_cast<StyleFlags>(0);

  /**
   * Font flags.
   * Whether the font can be embedded and subset in the file during PDF export depends on these flags.
   */
  enum FontFlags {
    // May be true for Type1, CFF, or TrueType fonts.
    Variable = 1 << 0,
    // May not be embedded.
    NotEmbeddable = 1 << 1,
    // May not be subset.
    NotSubsettable = 1 << 2,
    // Data compressed. Table access may still work.
    AltDataFormat = 1 << 3,
  };
  FontFlags flags = static_cast<FontFlags>(0);

  /**
   * Height of an upper-case letter.
   */
  float capHeight = 0.f;
};

}  // namespace tgfx
