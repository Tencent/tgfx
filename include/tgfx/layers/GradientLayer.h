/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Color.h"
#include "tgfx/layers/GradientType.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
/**
 * GradientLayer represents a layer that draws a color gradient.
 */
class GradientLayer : public Layer {
 public:
  /**
   * Creates a new gradient layer instance.
   */
  static std::shared_ptr<GradientLayer> Make();

  LayerType type() const override {
    return LayerType::Gradient;
  }

  /**
   * Returns the gradient type drawn by the layer.
   */
  GradientType gradientType() const {
    return _gradientType;
  }

  /**
   * Sets the gradient type drawn by the layer.
   */
  void setGradientType(GradientType gradientType);

  /**
   * Returns the start point of the gradient when drawn in the layer’s coordinate space. The start
   * point corresponds to the first stop of the gradient. The default start point is (0, 0).
   */
  const Point& startPoint() const {
    return _startPoint;
  }

  /**
   * Sets the start point of the gradient when drawn in the layer’s coordinate space.
   */
  void setStartPoint(const Point& startPoint);

  /**
   * Returns the end point of the gradient when drawn in the layer’s coordinate space. The end point
   * corresponds to the last stop of the gradient. The default end point is (0, 0).
   */
  const Point& endPoint() const {
    return _endPoint;
  }

  /**
   * Sets the end point of the gradient when drawn in the layer’s coordinate space.
   */
  void setEndPoint(const Point& endPoint);

  /**
   * Returns the array of colors to be distributed between the start and end points of the gradient.
   */
  const std::vector<Color>& colors() const {
    return _colors;
  }

  /**
   * Sets the array of colors to be distributed between the start and end points of the gradient.
   */
  void setColors(const std::vector<Color>& colors);

  /**
   * Returns the relative position of each corresponding color in the color array. If this is empty,
   * the colors are distributed evenly between the start and end point. If this is not empty, the
   * values must begin with 0, end with 1.0, and intermediate values must be strictly increasing.
   */
  const std::vector<float>& positions() const {
    return _positions;
  }

  /**
   * Sets the relative position of each corresponding color in the color array.
   */
  void setPositions(const std::vector<float>& positions);

 protected:
  GradientLayer() = default;

 private:
  GradientType _gradientType = GradientType::Linear;
  tgfx::Point _startPoint = tgfx::Point::Zero();
  tgfx::Point _endPoint = tgfx::Point::Zero();
  std::vector<tgfx::Color> _colors;
  std::vector<float> _positions;
};
}  // namespace tgfx
