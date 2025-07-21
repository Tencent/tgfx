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
    return static_cast<UserTypeface*>(typeface.get())->fontMetrics();
  }

  float getAdvance(GlyphID, bool) const override {
    return 0.0f;
  }

  Point getVerticalOffset(GlyphID) const override {
    return {};
  }
};
}  // namespace tgfx
