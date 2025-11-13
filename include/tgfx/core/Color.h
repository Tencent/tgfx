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

#include <cfloat>
#include <cinttypes>
#include <limits>
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

/**
 * RGBA color value, holding four floating point components. Color components are always in a known
 * order.
 */
struct Color {
  /**
   * Returns a fully transparent Color with colorSpace.
   */
  static const Color& Transparent(std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Returns a fully opaque black Color with colorSpace..
   */
  static const Color& Black(std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Returns a fully opaque white Color with colorSpace..
   */
  static const Color& White(std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Returns a fully opaque red Color with colorSpace..
   */
  static const Color& Red(std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Returns a fully opaque green Color with colorSpace..
   */
  static const Color& Green(std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Returns a fully opaque blue Color with colorSpace..
   */
  static const Color& Blue(std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Returns color value from 8-bit component values and ColorSpace.
   */
  static Color FromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255,
                        std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Constructs an opaque white Color.
   */
  Color() : red(1.0f), green(1.0f), blue(1.0f), alpha(1.0f), colorSpace(ColorSpace::MakeSRGB()) {
  }

  /**
   * Constructs a Color with the specified red, green, blue, alpha values and colorSpace. If the
   * color space is nullptr, it will be treated as sRGB.
   * @param r  red component
   * @param g  green component
   * @param b  blue component
   * @param a  alpha component
   * @param colorSpace colorSpace of this color
   */
  Color(float r, float g, float b, float a = 1.0f,
        std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB())
      : red(r), green(g), blue(b), alpha(a), colorSpace(std::move(colorSpace)) {
    if (this->colorSpace == nullptr) {
      this->colorSpace = ColorSpace::MakeSRGB();
    }
  }

  /**
   * Red component.
   */
  float red;

  /**
   * Green component.
   */
  float green;

  /**
   * Blue component.
   */
  float blue;

  /**
   * Alpha component.
   */
  float alpha;

  /**
   * ColorSpace of this Color.
   */
  std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB();

  /**
   * Compares Color with other, and returns true if all components are equal.
   */
  bool operator==(const Color& other) const {
    auto thisColorSpace = colorSpace;
    auto otherColorSpace = other.colorSpace;
    if (thisColorSpace == nullptr) {
      thisColorSpace = ColorSpace::MakeSRGB();
    }
    if (otherColorSpace == nullptr) {
      otherColorSpace = ColorSpace::MakeSRGB();
    }

    return alpha == other.alpha && red == other.red && green == other.green && blue == other.blue &&
           ColorSpace::Equals(thisColorSpace.get(), otherColorSpace.get());
  }

  /**
   * Compares Color with other, and returns true if not all components are equal.
   */
  bool operator!=(const Color& other) const {
    return !(*this == other);
  }

  /**
   * Returns a pointer to components of Color, for array access.
   */
  const float* array() const {
    return &red;
  }

  /**
   * Returns a pointer to components of Color, for array access.
   */
  float* array() {
    return &red;
  }

  /**
   * Returns one component.
   * @param index  one of: 0 (r), 1 (g), 2 (b), 3 (a)
   * @return value corresponding to index.
   */
  float operator[](int index) const;

  /**
   * Returns one component.
   * @param index  one of: 0 (r), 1 (g), 2 (b), 3 (a)
   * @return value corresponding to index.
   */
  float& operator[](int index);

  /**
   * Returns true if Color is an opaque color.
   */
  bool isOpaque() const;

  /**
   * Returns a Color with the alpha set to 1.0.
   */
  Color makeOpaque() const {
    return {red, green, blue, 1.0f, colorSpace};
  }

  /**
   * Returns a Color premultiplied by alpha.
   */
  Color premultiply() const {
    return {red * alpha, green * alpha, blue * alpha, alpha, colorSpace};
  }

  /**
   * Returns a Color unpremultiplied by alpha.
   */
  Color unpremultiply() const;

  /**
   * Return a new color, which is the original color assigned to the dst color space.
   */
  Color assignColorSpace(std::shared_ptr<ColorSpace> colorSpace) const;

  /**
   * Return a new color that is the original color converted to the dst color space.
   */
  Color convertColorSpace(std::shared_ptr<ColorSpace> colorSpace) const;
};
}  // namespace tgfx
