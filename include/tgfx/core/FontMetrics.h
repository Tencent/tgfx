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

#include <string>
namespace tgfx {
class FontMetrics {
 public:
  /**
   * Extent above baseline.
   */
  float top = 0;
  /**
   * Distance to reserve above baseline.
   */
  float ascent = 0;
  /**
   * Distance to reserve below baseline.
   */
  float descent = 0;
  /**
   * Extent below baseline.
   */
  float bottom = 0;
  /**
   * Distance to add between lines.
   */
  float leading = 0;
  /**
   * Minimum x.
   */
  float xMin = 0;
  /**
   * Maximum x.
   */
  float xMax = 0;
  /**
   * Height of lower-case 'x'.
   */
  float xHeight = 0;
  /**
   * Height of an upper-case letter.
   */
  float capHeight = 0;
  /**
   * Underline thickness.
   */
  float underlineThickness = 0;
  /**
   * Underline position relative to baseline.
   */
  float underlinePosition = 0;

  std::string postScriptName;

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

  StyleFlags style = static_cast<StyleFlags>(0);  // Font style characteristics.
  FontType type = FontType::Other;
  FontFlags flags = static_cast<FontFlags>(0);  // Global font flags.
};
}  // namespace tgfx
