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

#include <optional>
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

/**
 * Fills with a solid color modulated by the coverage of a Rect or RRect convolved with a Gaussian
 * kernel, computed analytically in a single pass. This is an internal shader used to render shadows
 * of shapes that can be expressed in closed form; it is not part of the public Shader API.
 *
 * The geometry and sigma are given in the same coordinate space, which does not have to match the
 * canvas local space: pass the scale between them as localScale.
 */
class ShapeBlurShader : public Shader {
 public:
  /**
   * Creates a shader for the given rounded rectangle. Returns nullptr when the inputs cannot
   * produce a closed-form result, in which case the caller must fall back to the filter path.
   * @param rRect the shape to blur. Must have at most one distinct corner radius (Rect, Simple or
   * Oval types); Complex is rejected.
   * @param sigmaX horizontal Gaussian standard deviation, in the same space as rRect.
   * @param sigmaY vertical Gaussian standard deviation, in the same space as rRect.
   * @param color the shadow color, unpremultiplied.
   * @param localScale the uniform scale from the canvas local space to the space of rRect and
   * sigma. Use 1 when they coincide.
   */
  static std::shared_ptr<Shader> Make(const RRect& rRect, float sigmaX, float sigmaY,
                                      const Color& color, float localScale);

  /**
   * Creates a shader for an inner shadow: the blurred coverage of shadowRRect is complemented and
   * then clipped to maskRRect. Returns nullptr when the inputs cannot produce a closed-form result.
   * @param maskRRect the shape the shadow is confined to, typically the layer's own outline. Its
   * corners must be circular, because the mask distance field is evaluated without sigma
   * normalization and so cannot express elliptical corners.
   * @param shadowRRect the shape the light passes through, i.e. maskRRect inset by the spread.
   * @param shadowCenterOffset the shadow center relative to the mask center, carrying the shadow
   * offset. Expressed in the same space as the two shapes.
   * @param sigmaX horizontal Gaussian standard deviation, in the same space as the two shapes.
   * @param sigmaY vertical Gaussian standard deviation, in the same space as the two shapes.
   * @param color the shadow color, unpremultiplied.
   * @param localScale the uniform scale from the canvas local space to the space of the shapes and
   * sigma. Use 1 when they coincide.
   */
  static std::shared_ptr<Shader> MakeInner(const RRect& maskRRect, const RRect& shadowRRect,
                                           const Point& shadowCenterOffset, float sigmaX,
                                           float sigmaY, const Color& color, float localScale);

  ShapeBlurShader(const RRect& rRect, const std::optional<RRect>& maskRRect,
                  const Point& shadowCenterOffset, float sigmaX, float sigmaY, const Color& color,
                  float localScale);

 protected:
  Type type() const override {
    return Type::ShapeBlur;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;

 private:
  PlacementPtr<FragmentProcessor> makeInnerProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const;

  RRect rRect = {};
  // Set for inner shadows only, where it confines the shadow to the layer's own shape.
  std::optional<RRect> maskRRect = {};
  Point shadowCenterOffset = {};
  float sigmaX = 0.0f;
  float sigmaY = 0.0f;
  Color color = Color::Transparent();
  float localScale = 1.0f;
};
}  // namespace tgfx
