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
 * Fills with the inner shadow of a rounded rectangle: the blurred coverage of the shape the light
 * passes through is complemented and then clipped to the mask, computed analytically in a single
 * pass. This is an internal shader used to render shadows of shapes that can be expressed in closed
 * form; it is not part of the public Shader API.
 *
 * The geometry and sigma are given in the same coordinate space, which does not have to match the
 * canvas local space: pass the scale between them as localScale.
 */
class RRectInnerShadowShader : public Shader {
 public:
  /**
   * Creates a shader for the given rounded rectangles. Returns nullptr when the inputs cannot produce
   * a closed-form result, in which case the caller must fall back to the filter path.
   * @param maskRect the bounds of the shape the shadow is confined to, typically the layer's own
   * outline.
   * @param maskRadius the mask's corner radius, shared by all four corners. The two components may
   * differ, describing an elliptical corner.
   * @param shadowRect the bounds of the shape the light passes through, i.e. maskRect inset by the
   * spread.
   * @param shadowRadius the shadow's corner radius, shared by all four corners. The two components
   * may differ, describing an elliptical corner.
   * @param shadowCenterOffset the shadow center relative to the mask center, carrying the shadow
   * offset. Expressed in the same space as the two shapes.
   * @param sigmaX horizontal Gaussian standard deviation, in the same space as the two shapes.
   * @param sigmaY vertical Gaussian standard deviation, in the same space as the two shapes.
   * @param color the shadow color, unpremultiplied.
   * @param localScale the uniform scale from the canvas local space to the space of the shapes and
   * sigma. Use 1 when they coincide.
   */
  static std::shared_ptr<Shader> Make(const Rect& maskRect, const Point& maskRadius,
                                      const Rect& shadowRect, const Point& shadowRadius,
                                      const Point& shadowCenterOffset, float sigmaX, float sigmaY,
                                      const Color& color, float localScale);

  RRectInnerShadowShader(const Rect& maskRect, const Point& maskRadius, const Rect& shadowRect,
                         const Point& shadowRadius, const Point& shadowCenterOffset, float sigmaX,
                         float sigmaY, const Color& color, float localScale);

  Rect maskRect = {};
  Point maskRadius = {};
  Rect shadowRect = {};
  Point shadowRadius = {};
  Point shadowCenterOffset = {};
  float sigmaX = 0.0f;
  float sigmaY = 0.0f;
  Color color = Color::Transparent();
  float localScale = 1.0f;

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
