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
 * Fills with the inner shadow of a rounded rectangle: the blurred coverage of the mask is
 * complemented and then clipped to the shadow shape, at a cost independent of sigma.
 */
class RRectInnerShadowShader : public Shader {
 public:
  /**
   * Creates a shader for the given rounded rectangles. Returns nullptr when the inputs cannot
   * produce a closed-form result. Zero radii produce the plain rectangle form.
   * @param shadowRRect the shadow shape. Its four corners must share one radius, whose two
   * components may differ to describe an elliptical corner. Must not be empty.
   * @param maskRRect the shape whose blurred coverage is subtracted from the shadow. Same
   * constraints as shadowRRect.
   * @param sigmaX horizontal Gaussian standard deviation, in the same space as the two shapes.
   * @param sigmaY vertical Gaussian standard deviation, in the same space as the two shapes.
   * @param color the shadow color, unpremultiplied.
   */
  static std::shared_ptr<Shader> Make(const RRect& shadowRRect, const RRect& maskRRect,
                                      float sigmaX, float sigmaY, const Color& color);

  RRectInnerShadowShader(const Rect& shadowRect, const Point& shadowRadius, const Rect& maskRect,
                         const Point& maskRadius, float sigmaX, float sigmaY, const Color& color);

  Rect shadowRect = {};
  Point shadowRadius = {};
  Rect maskRect = {};
  Point maskRadius = {};
  float sigmaX = 0.0f;
  float sigmaY = 0.0f;
  Color color = Color::Transparent();

 protected:
  Type type() const override {
    return Type::RRectInnerShadow;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;
};
}  // namespace tgfx
