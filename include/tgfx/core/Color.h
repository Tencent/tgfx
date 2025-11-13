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
   * Returns a fully transparent Color in srgb gamut.
   */
  static const Color& Transparent();

  /**
   * Returns a fully opaque black Color in srgb gamut.
   */
  static const Color& Black();

  /**
   * Returns a fully opaque white Color in srgb gamut.
   */
  static const Color& White();

  /**
   * Returns a fully opaque red Color in srgb gamut.
   */
  static const Color& Red();

  /**
   * Returns a fully opaque green Color in srgb gamut.
   */
  static const Color& Green();

  /**
   * Returns a fully opaque blue Color in srgb gamut.
   */
  static const Color& Blue();

  /**
   * Returns color value from 8-bit component values and ColorSpace.
   */
  static Color FromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255,
                        std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Constructs an opaque white Color.
   */
  Color() : red(1.0f), green(1.0f), blue(1.0f), alpha(1.0f), colorSpace(nullptr) {
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
  Color(float r, float g, float b, float a = 1.0f, std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : red(r), green(g), blue(b), alpha(a), colorSpace(std::move(colorSpace)) {
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
   * ColorSpace of this Color. If the color space is nullptr, it will be treated as sRGB.
   */
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  /**
   * Compares Color with other, and returns true if all components are equal.
   */
  bool operator==(const Color& other) const;

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
   * Return a new color that is the original color converted to the dst color space.
   */
  Color makeColorSpace(std::shared_ptr<ColorSpace> dstColorSpace) const;

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
};
}  // namespace tgfx
