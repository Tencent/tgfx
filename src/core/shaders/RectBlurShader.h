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

#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

/**
 * Fills with a solid color modulated by the coverage of a Rect convolved with a Gaussian kernel,
 * computed analytically in a single pass. This is an internal shader used to render shadows of
 * shapes that can be expressed in closed form; it is not part of the public Shader API.
 *
 * The geometry and sigma are given in the same coordinate space, which does not have to match the
 * canvas local space: pass the scale between them as localScale.
 */
class RectBlurShader : public Shader {
 public:
  /**
   * Creates a shader for the given rectangle. Returns nullptr when the inputs cannot produce a
   * closed-form result, in which case the caller must fall back to the filter path.
   * @param rect the shape to blur. Must not be empty.
   * @param sigmaX horizontal Gaussian standard deviation, in the same space as rect.
   * @param sigmaY vertical Gaussian standard deviation, in the same space as rect.
   * @param color the shadow color, unpremultiplied.
   * @param localScale the uniform scale from the canvas local space to the space of rect and sigma.
   * Use 1 when they coincide.
   */
  static std::shared_ptr<Shader> Make(const Rect& rect, float sigmaX, float sigmaY,
                                      const Color& color, float localScale);

  RectBlurShader(const Rect& rect, float sigmaX, float sigmaY, const Color& color,
                 float localScale);

  Rect rect = {};
  float sigmaX = 0.0f;
  float sigmaY = 0.0f;
  Color color = Color::Transparent();
  float localScale = 1.0f;

 protected:
  Type type() const override {
    return Type::RectBlur;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;
};
}  // namespace tgfx
