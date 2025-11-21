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

#include "gpu/processors/LumaFragmentProcessor.h"
namespace tgfx {
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

void LumaFragmentProcessor::computeProcessorKey(Context*, BytesKey* bytesKey) const {
  bytesKey->write(classID());
}

LumaFragmentProcessor::LumaFragmentProcessor(std::shared_ptr<ColorSpace> colorSpace)
    : FragmentProcessor(ClassID()), _colorSpace(std::move(colorSpace)) {
  ColorMatrix33 tempColorMatrix{};
  if (!_colorSpace) {
    _colorSpace = ColorSpace::SRGB();
  }
  _colorSpace->toXYZD50(&tempColorMatrix);
  _lumaFactor = AcquireLumaFactorFromColorSpace(tempColorMatrix);
}

LumaFragmentProcessor::LumaFactor LumaFragmentProcessor::AcquireLumaFactorFromColorSpace(
    const ColorMatrix33& matrix) {
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

}  // namespace tgfx
