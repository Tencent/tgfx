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

#include <array>
#include <memory>
#include "ColorSpace.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"

namespace tgfx {
template <typename T>
class PlacementPtr;
class FragmentProcessor;
class Context;

/**
 * ColorFilter is the base class for filters that perform color transformations in the drawing
 * pipeline.
 */
class ColorFilter {
 public:
  /**
   * Creates a color filter whose effect is to first apply the inner filter and then apply the outer
   * filter.
   */
  static std::shared_ptr<ColorFilter> Compose(std::shared_ptr<ColorFilter> inner,
                                              std::shared_ptr<ColorFilter> outer);

  /**
   * Creates a new ColorFilter that applies blends between the constant color (src) and input color
   * (dst) based on the BlendMode.
   */
  static std::shared_ptr<ColorFilter> Blend(Color color, BlendMode mode);

  /**
   * Creates a new ColorFilter that transforms the color using the given 4x5 matrix. The matrix can
   * be passed as a single array, and is treated as follows:
   *
   * [ a, b, c, d, e,
   *   f, g, h, i, j,
   *   k, l, m, n, o,
   *   p, q, r, s, t ]
   *
   * When applied to a color [R, G, B, A], the resulting color is computed as:
   *
   * R’ = a*R + b*G + c*B + d*A + e;
   * G’ = f*R + g*G + h*B + i*A + j;
   * B’ = k*R + l*G + m*B + n*A + o;
   * A’ = p*R + q*G + r*B + s*A + t;
   *
   * That resulting color [R’, G’, B’, A’] then has each channel clamped to the 0 to 1.0 range.
   */
  static std::shared_ptr<ColorFilter> Matrix(const std::array<float, 20>& rowMajor);

  /**
   * Creates a new ColorFilter that makes translucent colors fully opaque or fully transparent
   * based on a specified alpha threshold. Colors with alpha values below this threshold will
   * become fully transparent, while colors with alpha values at or above this threshold will
   * become fully opaque.
   */
  static std::shared_ptr<ColorFilter> AlphaThreshold(float threshold);

  /**
   * Creates a new ColorFilter that transforms the input color into its corresponding brightness.
   */
  static std::shared_ptr<ColorFilter> Luma();

  virtual ~ColorFilter() = default;

  /**
   * Returns true if the filter is guaranteed to never change the alpha of a color it filters.
   */
  virtual bool isAlphaUnchanged() const {
    return false;
  }

  /** 
   * If the filter can be represented by a source color plus Mode, this returns true, and sets (if
   * not NULL) the color and mode appropriately.If not, this returns false and ignores the
   * parameters.
   */
  virtual bool asColorMode(Color*, BlendMode*) const {
    return false;
  }

 protected:
  enum class Type { Blend, Matrix, AlphaThreshold, Compose, Luma };

  /**
   * Returns the type of this color filter.
   */
  virtual Type type() const = 0;

  /**
   * Returns true if the specified color filter is equivalent to this ColorFilter.
   */
  virtual bool isEqual(const ColorFilter* colorFilter) const = 0;

 private:
  virtual PlacementPtr<FragmentProcessor> asFragmentProcessor(
      Context* context,
      std::shared_ptr<ColorSpace> dstColorSpace = ColorSpace::MakeSRGB()) const = 0;

  friend class OpsCompositor;
  friend class ColorFilterShader;
  friend class ComposeColorFilter;
  friend class ColorImageFilter;
  friend class Types;
};
}  // namespace tgfx
