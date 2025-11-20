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

#include <cassert>
#include <cfloat>
#include <cinttypes>
#include <limits>
#include "tgfx/core/AlphaType.h"
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

/**
 * RGBA color value, holding four floating point components. Color components are always in a known
 * order.
 */
template <AlphaType AT>
struct RGBA4f {
  /**
   * Returns a fully transparent Color in srgb gamut.
   */
  static const RGBA4f& Transparent() {
    static const RGBA4f color = {0.0f, 0.0f, 0.0f, 0.0f};
    return color;
  }

  /**
   * Returns a fully opaque black Color in srgb gamut.
   */
  static const RGBA4f& Black() {
    static const RGBA4f color = {0.0f, 0.0f, 0.0f, 1.0f};
    return color;
  }

  /**
   * Returns a fully opaque white Color in srgb gamut.
   */
  static const RGBA4f& White() {
    static const RGBA4f color = {1.0f, 1.0f, 1.0f, 1.0f};
    return color;
  }

  /**
   * Returns a fully opaque red Color in srgb gamut.
   */
  static const RGBA4f& Red() {
    static const RGBA4f color = {1.0f, 0.0f, 0.0f, 1.0f};
    return color;
  }

  /**
   * Returns a fully opaque green Color in srgb gamut.
   */
  static const RGBA4f& Green() {
    static const RGBA4f color = {0.0f, 1.0f, 0.0f, 1.0f};
    return color;
  }

  /**
   * Returns a fully opaque blue Color in srgb gamut.
   */
  static const RGBA4f& Blue() {
    static const RGBA4f color = {0.0f, 0.0f, 1.0f, 1.0f};
    return color;
  }

  /**
   * Returns color value from 8-bit component values and ColorSpace.
   */
  static RGBA4f FromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255,
                         std::shared_ptr<ColorSpace> colorSpace = nullptr) {
    return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
            static_cast<float>(b) / 255.0f, a == 255 ? 1.0f : static_cast<float>(a) / 255.0f,
            std::move(colorSpace)};
  }

  /**
   * Constructs an opaque white Color.
   */
  RGBA4f() : red(1.0f), green(1.0f), blue(1.0f), alpha(1.0f), colorSpace(nullptr) {
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
  RGBA4f(float r, float g, float b, float a = 1.0f,
         std::shared_ptr<ColorSpace> colorSpace = nullptr)
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
   * Compares Color with other, and returns true if all components are equal. For performance
   * reasons, the comparison of internal colorspaces is done by merely checking if the pointer
   * addresses are equal.
   */
  bool operator==(const RGBA4f& other) const {
    return alpha == other.alpha && red == other.red && green == other.green && blue == other.blue &&
           colorSpace == other.colorSpace;
  }

  /**
   * Compares Color with other, and returns true if not all components are equal.
   */
  bool operator!=(const RGBA4f& other) const {
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
  float operator[](int index) const {
    assert(index >= 0 && index < 4);
    return (&red)[index];
  }

  /**
   * Returns one component.
   * @param index  one of: 0 (r), 1 (g), 2 (b), 3 (a)
   * @return value corresponding to index.
   */
  float& operator[](int index) {
    assert(index >= 0 && index < 4);
    return (&red)[index];
  }

  /**
   * Returns true if Color is an opaque color.
   */
  bool isOpaque() const {
    assert(alpha <= 1.0f && alpha >= 0.0f);
    return alpha == 1.0f;
  }

  /**
   * Returns a Color with the alpha set to 1.0.
   */
  RGBA4f makeOpaque() const {
    return {red, green, blue, 1.0f, colorSpace};
  }

  /**
   * Return a new color that is the original color converted to the dst color space. If the
   * dstColorSpace is nullptr, no convert.
   */
  RGBA4f makeColorSpace(std::shared_ptr<ColorSpace> dstColorSpace) const;

  /**
   * Returns a Color premultiplied by alpha. Asserts at compile time if RGBA4f is already
   * premultiplied.
   */
  RGBA4f<AlphaType::Premultiplied> premultiply() const {
    static_assert(AT == AlphaType::Unpremultiplied);
    return {red * alpha, green * alpha, blue * alpha, alpha, colorSpace};
  }

  /**
   * Returns a Color unpremultiplied by alpha. Asserts at compile time if RGBA4f is already
   * unpremultiplied.
   */
  RGBA4f<AlphaType::Unpremultiplied> unpremultiply() const {
    static_assert(AT == AlphaType::Premultiplied);
    if (alpha == 0.0f) {
      return {0, 0, 0, 0, colorSpace};
    } else {
      float invAlpha = 1 / alpha;
      return {red * invAlpha, green * invAlpha, blue * invAlpha, alpha, colorSpace};
    }
  }
};

template <>
RGBA4f<AlphaType::Unpremultiplied> RGBA4f<AlphaType::Unpremultiplied>::makeColorSpace(
    std::shared_ptr<ColorSpace> dstColorSpace) const;

template <>
RGBA4f<AlphaType::Premultiplied> RGBA4f<AlphaType::Premultiplied>::makeColorSpace(
    std::shared_ptr<ColorSpace> dstColorSpace) const;

/**
 * For convenience, Color is an alias for RGBA4f<AlphaType::Unpremultiplied>.
 */
using Color = RGBA4f<AlphaType::Unpremultiplied>;

/**
 * For convenience, PMColor is an alias for RGBA4f<AlphaType::Premultiplied>.
 */
using PMColor = RGBA4f<AlphaType::Premultiplied>;

}  // namespace tgfx
