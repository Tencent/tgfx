/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/BlendMode.h"
#include "tgfx/core/ColorFilter.h"

namespace tgfx {
class ModeColorFilter : public ColorFilter {
 public:
  ModeColorFilter(Color color, BlendMode mode) : color(color), mode(mode) {
  }

  bool isAlphaUnchanged() const override;

  bool asColorMode(Color* color, BlendMode* mode) const override;

  Color color;
  BlendMode mode;

 protected:
  Type type() const override {
    return Type::Blend;
  }

  bool isEqual(const ColorFilter* colorFilter) const override;

 private:
  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      Context* context, std::shared_ptr<ColorSpace> dstColorSpace) const override;
};
}  // namespace tgfx
