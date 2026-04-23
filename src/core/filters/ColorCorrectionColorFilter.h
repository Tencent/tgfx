/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/core/ColorFilter.h"

namespace tgfx {
class ColorCorrectionColorFilter : public ColorFilter {
 public:
  ColorCorrectionColorFilter(float exposure, float contrast, float saturation, float temperature,
                             float tint, float highlights, float shadows)
      : exposure(exposure), contrast(contrast), saturation(saturation), temperature(temperature),
        tint(tint), highlights(highlights), shadows(shadows) {
  }

  bool isAlphaUnchanged() const override {
    return true;
  }

  float exposure = 0.0f;
  float contrast = 0.0f;
  float saturation = 0.0f;
  float temperature = 0.0f;
  float tint = 0.0f;
  float highlights = 0.0f;
  float shadows = 0.0f;

 protected:
  Type type() const override {
    return Type::ColorCorrection;
  }

  bool isEqual(const ColorFilter* colorFilter) const override;

 private:
  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      Context* context, const std::shared_ptr<ColorSpace>& dstColorSpace) const override;
};
}  // namespace tgfx
