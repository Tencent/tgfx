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

#include "tgfx/core/ImageFilter.h"

namespace tgfx {
class DropShadowImageFilter : public ImageFilter {
 public:
  DropShadowImageFilter(float dx, float dy, float blurrinessX, float blurrinessY,
                        const Color& color, bool shadowOnly);

 protected:
  Type type() const override {
    return Type::DropShadow;
  };

  Rect onFilterBounds(const Rect& srcRect) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                         const FPArgs& args,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix) const override;

 public:
  float dx = 0;
  float dy = 0;
  std::shared_ptr<ImageFilter> blurFilter = nullptr;
  Color color = Color::Black();
  bool shadowOnly = false;
};
}  // namespace tgfx
