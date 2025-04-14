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

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * Round Rect.
 */
struct RRect {
  Rect rect = {};
  Point radii = {};

  /**
   * Returns true if the bounds are a simple rectangle.
   */
  bool isRect() const;

  /**
   * Returns true if the bounds are an oval.
   */
  bool isOval() const;

  /**
   * Sets to rounded rectangle with the same radii for all four corners.
   * @param rect  bounds of rounded rectangle
   * @param radiusX  x-axis radius of corners
   * @param radiusY  y-axis radius of corners
   */
  void setRectXY(const Rect& rect, float radiusX, float radiusY);

  /**
   * Sets bounds to oval, x-axis radii to half oval.width(), and all y-axis radii to half
   * oval.height().
   */
  void setOval(const Rect& oval);

  /**
   * Scale the round rectangle by scaleX and scaleY.
   */
  void scale(float scaleX, float scaleY);
};
}  // namespace tgfx
