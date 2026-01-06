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

namespace tgfx {
/**
 * Defines the type of gradient to be drawn.
 */
enum class GradientType {
  /*
   * None is used to indicate that the gradient type is not defined.
  */
  None,
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
  /**
   * Diamond gradients are defined by a center point and a half-diagonal. The color gradient is
   * drawn from the center point to the vertices of the diamond.
   */
  Diamond
};

}  // namespace tgfx
