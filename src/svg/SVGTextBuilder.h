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

#include <MacTypes.h>
#include <string>
#include "tgfx/core/GlyphRun.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
class SVGTextBuilder {
 public:
  explicit SVGTextBuilder(const GlyphRun& glyphRun);

  std::string posX() const {
    return posXString;
  }

  std::string posY() const {
    return hasConstY ? constYString : posYString;
  }

  std::string text() const {
    return _text;
  }

 private:
  void appendUnichar(Unichar c, Point position);

  std::string _text;
  std::string posXString;
  std::string posYString;
  std::string constYString;
  float constY;
  bool lastCharWasWhitespace = true;
  bool hasConstY = true;
};
}  // namespace tgfx