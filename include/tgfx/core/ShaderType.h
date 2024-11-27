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

#include <array>
#include <vector>
#include "tgfx/core/Color.h"
#include "tgfx/core/Point.h"

namespace tgfx {

enum class ShaderType {
  Color,
  ColorFilter,
  Image,
  Blend,
  Matrix,
  Gradient,
};

/**
 *  Linear:
 *      points[0] and points[1] represent the start and end points of the gradient.
 *  Radial:
 *      points[0] and radiuses[0] represent the center and radius of the gradient.
 *  Conic:
 *      points[0] represents the center, and radiuses[0] and radiuses[1] represent the start angle
 *      and end angle of the gradient.
 */
struct GradientInfo {
  std::vector<Color> colors;     // The colors in the gradient
  std::vector<float> positions;  // The positions of the colors in the gradient
  std::array<Point, 2> points;
  std::array<float, 2> radiuses;
};

/**
 * Defines the type of gradient to be drawn.
 */
enum class GradientType {
  /**
   * Linear gradients are defined by an axis, which is a line that the color gradient is aligned
   * with.
   */
  Linear,
  /**
   * Radial gradients are defined by a center point and a radius. The color gradient is drawn from
   * the center point to the edge of the radius.
   */
  Radial,
  /**
   * Conic gradients are defined by a center point and an angular range. The color gradient is drawn
   * from the start angle to the end angle, wrapping around the center point.
   */
  Conic,

  /*
   * None is used to indicate that the gradient type is not defined.
  */
  None,
};

}  // namespace tgfx