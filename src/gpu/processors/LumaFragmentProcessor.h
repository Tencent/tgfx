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
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
struct LumaFactor {
  /** default ITU-R Recommendation BT.709 at http://www.itu.int/rec/R-REC-BT.709/ .*/
  float kr = 0.2126f;
  float kg = 0.7152f;
  float kb = 0.0722f;
};

static bool NearlyEqual(float x, float y) {
  static constexpr float Tolerance = 1.0f / (1 << 11);
  return ::fabsf(x - y) <= Tolerance;
}

static bool NearlyEqual(const ColorMatrix33& u, const ColorMatrix33& v) {
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      if (!NearlyEqual(u.values[r][c], v.values[r][c])) {
        return false;
      }
    }
  }
  return true;
}

static LumaFactor AcquireLumaFactorFromColorSpace(const ColorMatrix33& matrix) {
  ColorMatrix33 tempColorMatrix{};

  NamedPrimaries::Rec601.toXYZD50(&tempColorMatrix);
  if (NearlyEqual(matrix, tempColorMatrix)) {
    return {0.299f, 0.587f, 0.114f};
  }

  NamedPrimaries::Rec2020.toXYZD50(&tempColorMatrix);
  if (NearlyEqual(matrix, tempColorMatrix)) {
    return {0.2627f, 0.678f, 0.0593f};
  }

  if (NearlyEqual(matrix, NamedGamut::AdobeRGB)) {
    return {0.2973f, 0.6274f, 0.0753f};
  }
  return LumaFactor{};
}

class LumaFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<FragmentProcessor> Make(
      BlockBuffer* buffer, std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  std::string name() const override {
    return "LumaFragmentProcessor";
  }

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  LumaFragmentProcessor(std::shared_ptr<ColorSpace> colorSpace)
      : FragmentProcessor(ClassID()), _colorSpace(std::move(colorSpace)) {
    ColorMatrix33 tempColorMatrix{};
    _colorSpace->toXYZD50(&tempColorMatrix);
    _lumaFactor = AcquireLumaFactorFromColorSpace(tempColorMatrix);
  }

  std::shared_ptr<ColorSpace> _colorSpace = ColorSpace::MakeSRGB();
  LumaFactor _lumaFactor;
};
}  // namespace tgfx
