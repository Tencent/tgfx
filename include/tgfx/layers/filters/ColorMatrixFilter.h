/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * A filter that transforms the color using the given 4x5 matrix.
 */
class ColorMatrixFilter : public LayerFilter {
 public:
  /**
   * Creates a new ColorMatrixFilter that transforms the color using the given 4x5 matrix. The matrix can
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
  static std::shared_ptr<ColorMatrixFilter> Make(const std::array<float, 20>& matrix);

  /**
   * Returns the color matrix used by this filter.
   */
  std::array<float, 20> matrix() const {
    return _matrix;
  }

  /**
   * Sets the color matrix to use.
   * @param matrix
   */
  void setMatrix(const std::array<float, 20>& matrix);

 protected:
  Type type() const override {
    return Type::ColorMatrixFilter;
  }

  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

 private:
  ColorMatrixFilter(const std::array<float, 20>& matrix);
  std::array<float, 20> _matrix;
};
}  // namespace tgfx
