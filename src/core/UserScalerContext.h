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
#include "UserTypeface.h"
#include "core/ScalerContext.h"

namespace tgfx {

class UserScalerContext : public ScalerContext {
 public:
  UserScalerContext(std::shared_ptr<Typeface> typeface, float size)
      : ScalerContext(std::move(typeface), size) {
  }

  FontMetrics getFontMetrics() const override {
    auto userTypeface = static_cast<UserTypeface*>(typeface.get());
    float scale = textSize / static_cast<float>(userTypeface->unitsPerEm());
    const auto& metrics = userTypeface->fontMetrics();
    FontMetrics result = {};
    result.top = metrics.top * scale;
    result.ascent = metrics.ascent * scale;
    result.descent = metrics.descent * scale;
    result.bottom = metrics.bottom * scale;
    result.leading = metrics.leading * scale;
    result.xMin = metrics.xMin * scale;
    result.xMax = metrics.xMax * scale;
    result.xHeight = metrics.xHeight * scale;
    result.capHeight = metrics.capHeight * scale;
    result.underlineThickness = metrics.underlineThickness * scale;
    result.underlinePosition = metrics.underlinePosition * scale;
    return result;
  }

  float getAdvance(GlyphID, bool) const override {
    return 0.0f;
  }

  Point getVerticalOffset(GlyphID) const override {
    return {};
  }
};
}  // namespace tgfx
