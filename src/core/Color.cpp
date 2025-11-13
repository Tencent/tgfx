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

#include "tgfx/core/Color.h"
#include "core/ColorSpaceXformSteps.h"
#include "core/utils/Log.h"
#include "tgfx/core/AlphaType.h"

namespace tgfx {
Color Color::Transparent(std::shared_ptr<ColorSpace> colorSpace) {
  Color color = {0.0f, 0.0f, 0.0f, 0.0f, std::move(colorSpace)};
  return color;
}

Color Color::Black(std::shared_ptr<ColorSpace> colorSpace) {
  Color color = {0.0f, 0.0f, 0.0f, 1.0f, std::move(colorSpace)};
  return color;
}

Color Color::White(std::shared_ptr<ColorSpace> colorSpace) {
  Color color = {1.0f, 1.0f, 1.0f, 1.0f, std::move(colorSpace)};
  return color;
}

Color Color::Red(std::shared_ptr<ColorSpace> colorSpace) {
  Color color = {1.0f, 0.0f, 0.0f, 1.0f, std::move(colorSpace)};
  return color;
}

Color Color::Green(std::shared_ptr<ColorSpace> colorSpace) {
  Color color = {0.0f, 1.0f, 0.0f, 1.0f, std::move(colorSpace)};
  return color;
}

Color Color::Blue(std::shared_ptr<ColorSpace> colorSpace) {
  Color color = {0.0f, 0.0f, 1.0f, 1.0f, std::move(colorSpace)};
  return color;
}

Color Color::FromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                      std::shared_ptr<ColorSpace> colorSpace) {
  return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
          static_cast<float>(b) / 255.0f, a == 255 ? 1.0f : static_cast<float>(a) / 255.0f,
          std::move(colorSpace)};
}

float Color::operator[](int index) const {
  DEBUG_ASSERT(index >= 0 && index < 4);
  return (&red)[index];
}

float& Color::operator[](int index) {
  DEBUG_ASSERT(index >= 0 && index < 4);
  return (&red)[index];
}

bool Color::isOpaque() const {
  DEBUG_ASSERT(alpha <= 1.0f && alpha >= 0.0f);
  return alpha == 1.0f;
}

Color Color::unpremultiply() const {
  if (alpha == 0.0f) {
    return {0, 0, 0, 0, colorSpace};
  } else {
    float invAlpha = 1 / alpha;
    return {red * invAlpha, green * invAlpha, blue * invAlpha, alpha, colorSpace};
  }
}

Color Color::assignColorSpace(std::shared_ptr<ColorSpace> colorSpace) const {
  return {red, green, blue, alpha, std::move(colorSpace)};
}

Color Color::convertColorSpace(std::shared_ptr<ColorSpace> colorSpace) const {
  return ColorSpaceXformSteps::ConvertColorSpace(*this, std::move(colorSpace));
}

}  // namespace tgfx
