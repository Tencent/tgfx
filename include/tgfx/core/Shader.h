/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <memory>
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/TileMode.h"

namespace tgfx {
class FragmentProcessor;

/**
 * Shaders specify the source color(s) for what is being drawn. If a paint has no shader, then the
 * paint's color is used. If the paint has a shader, then the shader's color(s) are used instead,
 * but they are modulated by the paint's alpha. Shaders are in
 */
class Shader {
 public:
  /**
   * Creates a shader that draws the specified color.
   */
  static std::shared_ptr<Shader> MakeColorShader(Color color);

  /**
   * Creates a shader that draws the specified image.
   */
  static std::shared_ptr<Shader> MakeImageShader(std::shared_ptr<Image> image,
                                                 TileMode tileModeX = TileMode::Clamp,
                                                 TileMode tileModeY = TileMode::Clamp,
                                                 SamplingOptions sampling = {});

  /**
   * Creates a shader that blends the two specified shaders.
   */
  static std::shared_ptr<Shader> MakeBlend(BlendMode mode, std::shared_ptr<Shader> dst,
                                           std::shared_ptr<Shader> src);

  /**
   * Returns a shader that generates a linear gradient between the two specified points.
   * @param startPoint The start point for the gradient.
   * @param endPoint The end point for the gradient.
   * @param colors The array of colors, to be distributed between the two points.
   * @param positions Maybe empty. The relative position of each corresponding color in the color
   * array. If this is empty, the colors are distributed evenly between the start and end point.
   * If this is not empty, the values must begin with 0, end with 1.0, and intermediate values must
   * be strictly increasing.
   */
  static std::shared_ptr<Shader> MakeLinearGradient(const Point& startPoint, const Point& endPoint,
                                                    const std::vector<Color>& colors,
                                                    const std::vector<float>& positions);

  /**
   * Returns a shader that generates a radial gradient given the center and radius.
   * @param center The center of the circle for this gradient
   * @param radius Must be positive. The radius of the circle for this gradient.
   * @param colors The array of colors, to be distributed between the center and edge of the circle.
   * @param positions Maybe empty. The relative position of each corresponding color in the color
   * array. If this is empty, the colors are distributed evenly between the start and end point.
   * If this is not empty, the values must begin with 0, end with 1.0, and intermediate values must
   * be strictly increasing.
   */
  static std::shared_ptr<Shader> MakeRadialGradient(const Point& center, float radius,
                                                    const std::vector<Color>& colors,
                                                    const std::vector<float>& positions);

  /**
   * Returns a shader that generates a sweep gradient given a center.
   * @param center The center of the circle for this gradient
   * @param startAngle Start of the angular range, corresponding to pos == 0.
   * @param endAngle End of the angular range, corresponding to pos == 1.
   * @param colors The array of colors, to be distributed around the center, within the gradient
   * angle range.
   * @param positions Maybe empty. The relative position of each corresponding color in the color
   * array. If this is empty, the colors are distributed evenly between the start and end point.
   * If this is not empty, the values must begin with 0, end with 1.0, and intermediate values must
   * be strictly increasing.
   */
  static std::shared_ptr<Shader> MakeSweepGradient(const Point& center, float startAngle,
                                                   float endAngle, const std::vector<Color>& colors,
                                                   const std::vector<float>& positions);

  virtual ~Shader() = default;

  /**
   * Returns true if the shader is guaranteed to produce only opaque colors, subject to the Paint
   * using the shader to apply an opaque alpha value. Subclasses should override this to allow some
   * optimizations.
   */
  virtual bool isOpaque() const {
    return false;
  }

  /**
   * If the shader has a constant color, this method returns true and updates the color parameter.
   * Otherwise, it returns false and leaves the color parameter unchanged.
   */
  virtual bool asColor(Color*) const {
    return false;
  }

  /**
   * Returns a shader that will apply the specified viewMatrix to this shader when drawing. The
   * specified matrix will be applied after any matrix associated with this shader.
   */
  virtual std::shared_ptr<Shader> makeWithMatrix(const Matrix& viewMatrix) const;

  /**
   * Create a new shader that produces the same colors as invoking this shader and then applying
   * the ColorFilter.
   */
  std::shared_ptr<Shader> makeWithColorFilter(std::shared_ptr<ColorFilter> colorFilter) const;

 protected:
  std::weak_ptr<Shader> weakThis;

  virtual std::unique_ptr<FragmentProcessor> asFragmentProcessor(
      const DrawArgs& args, const Matrix* localMatrix) const = 0;

  friend class FragmentProcessor;
  friend class Canvas;
};
}  // namespace tgfx
