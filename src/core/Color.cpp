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
#include "utils/ColorSpaceHelper.h"

namespace tgfx {
const Color& Color::Transparent() {
  static const Color color = {0.0f, 0.0f, 0.0f, 0.0f};
  return color;
}

const Color& Color::Black() {
  static const Color color = {0.0f, 0.0f, 0.0f, 1.0f};
  return color;
}

const Color& Color::White() {
  static const Color color = {1.0f, 1.0f, 1.0f, 1.0f};
  return color;
}

const Color& Color::Red() {
  static const Color color = {1.0f, 0.0f, 0.0f, 1.0f};
  return color;
}

const Color& Color::Green() {
  static const Color color = {0.0f, 1.0f, 0.0f, 1.0f};
  return color;
}

const Color& Color::Blue() {
  static const Color color = {0.0f, 0.0f, 1.0f, 1.0f};
  return color;
}

Color Color::FromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                      std::shared_ptr<ColorSpace> colorSpace) {
  return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
          static_cast<float>(b) / 255.0f, a == 255 ? 1.0f : static_cast<float>(a) / 255.0f,
          std::move(colorSpace)};
}

bool Color::operator==(const Color& other) const {
  auto thisColorSpace = colorSpace;
  auto otherColorSpace = other.colorSpace;
  if (thisColorSpace == nullptr) {
    thisColorSpace = ColorSpace::SRGB();
  }
  if (otherColorSpace == nullptr) {
    otherColorSpace = ColorSpace::SRGB();
  }

  return alpha == other.alpha && red == other.red && green == other.green && blue == other.blue &&
         ColorSpace::Equals(thisColorSpace.get(), otherColorSpace.get());
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

Color Color::makeColorSpace(std::shared_ptr<ColorSpace> colorSpace) const {
  auto dstColor = *this;
  if (colorSpace == nullptr) {
    colorSpace = ColorSpace::SRGB();
  }
  if (!NeedConvertColorSpace(this->colorSpace, colorSpace)) {
    return dstColor;
  }
  ColorSpaceXformSteps steps(this->colorSpace.get(), AlphaType::Unpremultiplied, colorSpace.get(),
                             AlphaType::Unpremultiplied);
  steps.apply(dstColor.array());
  dstColor.colorSpace = colorSpace;
  return dstColor;
}

}  // namespace tgfx
