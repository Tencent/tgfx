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

#include "tgfx/core/Color.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Size.h"

namespace tgfx {
class InnerShadowImageFilter : public ImageFilter {
 public:
  InnerShadowImageFilter(float dx, float dy, float blurrinessX, float blurrinessY,
                         const Color& color, bool shadowOnly);

  Type type() const override {
    return Type::InnerShadow;
  };

  bool isShadowOnly() const {
    return shadowOnly;
  }

  Color shadowColor() const {
    return color;
  }

  Point offset() const;

  Size blurSize() const;

 protected:
  std::unique_ptr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                         const FPArgs& args,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix) const override;

 private:
  float dx = 0.0f;
  float dy = 0.0f;
  std::shared_ptr<ImageFilter> blurFilter = nullptr;
  Color color = Color::Black();
  bool shadowOnly = false;
};
}  // namespace tgfx
