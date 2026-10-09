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

#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

/**
 * Fills with a solid color modulated by the coverage of a rounded rectangle convolved with a
 * Gaussian kernel truncated at +/- 2 sigma, at a cost independent of sigma.
 */
class RRectBlurShader : public Shader {
 public:
  /**
   * Creates a shader for the given rounded rectangle. Returns nullptr when the inputs cannot
   * produce a closed-form result.
   * @param rRect the shape to blur. Its four corners must share one radius, whose two components
   * may differ to describe an elliptical corner; a zero radius produces a rectangle. Must not be
   * empty.
   * @param sigmaX horizontal Gaussian standard deviation, in the same space as rRect.
   * @param sigmaY vertical Gaussian standard deviation, in the same space as rRect.
   * @param color the shadow color, unpremultiplied.
   */
  static std::shared_ptr<Shader> Make(const RRect& rRect, float sigmaX, float sigmaY,
                                      const Color& color);

  RRectBlurShader(const Rect& rect, const Point& radius, float sigmaX, float sigmaY,
                  const Color& color);

  Rect rect = {};
  Point radius = {};
  float sigmaX = 0.0f;
  float sigmaY = 0.0f;
  Color color = Color::Transparent();

 protected:
  Type type() const override {
    return Type::RRectBlur;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;
};
}  // namespace tgfx
