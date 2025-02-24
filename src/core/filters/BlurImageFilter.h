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

#include "tgfx/core/ImageFilter.h"

namespace tgfx {
class BlurImageFilter : public ImageFilter {
 public:
  BlurImageFilter(float blurrinessX, float blurrinessY, TileMode tileMode)
      : ImageFilter(), blurrinessX(blurrinessX), blurrinessY(blurrinessY), tileMode(tileMode) {
  }

  float blurrinessX = 0.0f;
  float blurrinessY = 0.0f;
  TileMode tileMode = TileMode::Decal;

 protected:
  Type type() const override {
    return Type::Blur;
  }
};

}  // namespace tgfx
