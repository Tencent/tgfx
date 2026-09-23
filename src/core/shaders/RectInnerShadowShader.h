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
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

/**
 * Fills with the inner shadow of a rectangle: the blurred coverage of the mask is complemented and
 * then clipped to the shadow shape, computed analytically, so its cost does not depend on sigma.
 * This is an internal shader used to render shadows of shapes that can be expressed in closed form;
 * it is not part of the public Shader API.
 *
 * The geometry and sigma are given in the local space of the draw, i.e. before the canvas matrix
 * maps them to device space. Wrap the shader with Shader::makeWithMatrix() when they live in a
 * different space.
 */
class RectInnerShadowShader : public Shader {
 public:
  /**
   * Creates a shader for the given rectangles. Returns nullptr when the inputs cannot produce a
   * closed-form result, in which case the caller must fall back to the filter path.
   * @param shadowRect the shape the shadow is confined to, typically the layer's own outline.
   * @param maskRect the shape whose blurred coverage is subtracted from the shadow, i.e.
   * shadowRect inset by the spread and displaced by the shadow offset.
   * @param sigmaX horizontal Gaussian standard deviation, in the same space as the two shapes.
   * @param sigmaY vertical Gaussian standard deviation, in the same space as the two shapes.
   * @param color the shadow color, unpremultiplied.
   */
  static std::shared_ptr<Shader> Make(const Rect& shadowRect, const Rect& maskRect, float sigmaX,
                                      float sigmaY, const Color& color);

  RectInnerShadowShader(const Rect& shadowRect, const Rect& maskRect, float sigmaX, float sigmaY,
                        const Color& color);

  Rect shadowRect = {};
  Rect maskRect = {};
  float sigmaX = 0.0f;
  float sigmaY = 0.0f;
  Color color = Color::Transparent();

 protected:
  Type type() const override {
    return Type::RectInnerShadow;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;
};
}  // namespace tgfx
